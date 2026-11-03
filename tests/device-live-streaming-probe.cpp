// Standalone C++17 native/device probe; deliberately not registered in CMake.
// Requires the isolated-pending ChunkPump contract: worker never accesses live
// World; poll/finish commit on the owner thread; reset/destruction discard.
// Link renderer_runtime, world_runtime, original modules and -pthread.
// Use stdio (no iostream/stoi); check the final ELF's symbol versions on host.
// The mkdtemp root is retained and printed for inspection, never a user world.
#include "BlockCollision.hpp"
#include "ChunkPump.hpp"
#include "Inventory.hpp"
#include "Scene.hpp"
#include "WorldRenderer.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include <unistd.h>

namespace {
using Clock = std::chrono::steady_clock;
using mcworld::ChunkPump;
using mcworld::ChunkRequest;
using mcworld::StreamingInfo;
using mcworld::World;
using State = ChunkPump::State;
constexpr int Radius = 2, Target = 25, Budget = 8;
constexpr int Width = 320, Height = 180;
constexpr float Aspect = float(Width) / Height;
constexpr float Dt = .02f;
constexpr uint32_t Seed = 12345;
constexpr int WorldSize = 5120;

void require(bool condition, const char* message) {
    // Unlike assert(), these checks and their operands also execute with NDEBUG.
    if (!condition) throw std::runtime_error(message);
}

double elapsedMs(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

bool sameRequest(const ChunkRequest& a, const ChunkRequest& b) {
    return a.epoch == b.epoch && a.seed == b.seed && a.worldSize == b.worldSize &&
           a.key == b.key && a.structures == b.structures && a.decoration == b.decoration;
}

void unchangedLive(const StreamingInfo& before, const StreamingInfo& after) {
    require(after.enabled && after.radius == Radius && after.target == Target,
            "live streaming configuration changed during generation");
    require(after.centerChunkX == before.centerChunkX && after.centerChunkZ == before.centerChunkZ,
            "zero movement unexpectedly changed the streaming center");
    require(after.loaded == before.loaded && after.queued == before.queued &&
            after.resident == before.resident && after.totalGenerated == before.totalGenerated,
            "worker or ordinary live work published chunks before owner poll");
}

void printKeys(const char* label, const std::vector<ChunkRequest>& requests) {
    std::printf("%s", label);
    for (const auto& request : requests) std::printf(" %d", request.key);
    std::putchar('\n');
}

size_t rankOf(const std::vector<ChunkRequest>& requests, int key) {
    for (size_t i = 0; i < requests.size(); ++i)
        if (requests[i].key == key) return i;
    throw std::runtime_error("expected outer-row priority-test chunk is not queued");
}

void viewPriority(World& world, bool log) {
    const auto before = world.streamingInfo();
    const auto revision = world.revision();
    // OriginalWorld::orderQueue: safety center, conservative visible tier, then
    // distance/key. Use opposite radius-two rows for an unambiguous front/behind
    // priority check, not an assumed yaw tie-break among visible neighbors.
    world.setStreamingView(0, 0, Aspect); // +Z
    const auto forward = world.chunkRequests(Target);
    const auto repeated = world.chunkRequests(Target);
    require(forward.size() == size_t(before.queued) && forward.size() == repeated.size(),
            "chunkRequests did not return the complete unchanged queue");
    require(std::equal(forward.begin(), forward.end(), repeated.begin(), sameRequest),
            "const chunkRequests consumed or changed requests");
    world.setStreamingView(180, 0, Aspect); // -Z
    const auto backward = world.chunkRequests(Target);
    require(forward.size() == backward.size(), "turning changed the queued key set");
    for (const auto& request : forward) {
        require(request.epoch != 0 && request.seed == Seed && request.worldSize == WorldSize &&
                request.structures && request.decoration,
                "request did not preserve epoch/seed/size/generation flags");
        require(!world.chunkLoaded((request.key % (WorldSize / 16)) * 16,
                                   (request.key / (WorldSize / 16)) * 16),
                "request contains an already-loaded chunk");
        require(std::count_if(backward.begin(), backward.end(), [&](const ChunkRequest& other) {
                    return sameRequest(request, other);
                }) == 1, "turning lost/duplicated a request or changed its epoch");
    }
    const int chunks = WorldSize / 16;
    const int front = (before.centerChunkZ + Radius) * chunks + before.centerChunkX;
    const int back = (before.centerChunkZ - Radius) * chunks + before.centerChunkX;
    require(rankOf(forward, front) < rankOf(forward, back),
            "+Z visible row was not prioritized ahead of the behind-camera row");
    require(rankOf(backward, back) < rankOf(backward, front),
            "turning to -Z did not reprioritize the visible row");
    world.setStreamingView(0, 0, Aspect);
    unchangedLive(before, world.streamingInfo());
    require(world.revision() == revision, "view-only queue ordering changed terrain revision");
    if (log) {
        printKeys("VIEW +Z queued_keys=", forward);
        printKeys("VIEW -Z queued_keys=", backward);
    }
}

void collisionSanity() {
    // Small API assertions only; no physics fixture or terrain mutations.
    for (int tile : {8, 9, 10, 11, 31, 37, 50, 106}) {
        const auto boxes = mcworld::blockCollisionBoxes({tile, 0}, 0, 0, 0);
        require(boxes.count == 0 && !boxes.conservative, "null-AABB tile became a full-cell collider");
    }
    const auto ladder = mcworld::blockCollisionBoxes({65, 2}, 0, 0, 0);
    require(ladder.count == 1 && !ladder.conservative &&
            ladder.boxes[0].z1 - ladder.boxes[0].z0 == .125f,
            "ladder backing collider is not 2/16 thick");
    require(mcworld::collisionLadder({65, 2}) && mcworld::collisionLadder({106, 0}),
            "ladder/vine climb classification failed");
}

[[noreturn]] void workerFailure(ChunkPump& pump) {
    const auto error = pump.error();
    std::fprintf(stderr, "ChunkPump Failed: %s\n", error.c_str());
    (void)pump.finish(); // Preserve/rethrow the actual generation/commit exception.
    throw std::runtime_error("ChunkPump reported Failed but finish did not rethrow");
}

struct Counters {
    uint64_t iterations = 0, runningIterations = 0;
    uint64_t worldUpdates = 0, sceneUpdates = 0, frameCalls = 0;
    uint64_t inventoryReads = 0, statusReads = 0, saves = 0, runningSaves = 0;
    uint64_t runningTicks = 0, pixelHash = 0, readBytes = 0;
    int batches = 0, committed = 0;
    double maxMainFrameMs = 0, maxPollMs = 0;
};

void liveProbe(const char* root) {
    collisionSanity();
    World world;
    world.beginStreaming(Radius);
    const auto createStart = Clock::now();
    world.create(Seed, root, true, WorldSize, true, true);
    const auto initial = world.streamingInfo();
    require(world.loaded() && world.structuresEnabled() && world.decorationsEnabled(),
            "probe must use a loaded original structures+decoration world");
    require(initial.enabled && initial.radius == Radius && initial.target == Target &&
            initial.loaded > 0 && initial.loaded < Target && initial.queued > 0 &&
            initial.loaded + initial.queued == Target,
            "create must leave real non-spawn streaming work queued");
    std::printf("CREATE root=%s path=%s ms=%.3f loaded=%d queued=%d target=%d\n",
                root, world.path().c_str(), elapsedMs(createStart),
                initial.loaded, initial.queued, initial.target);
    std::fflush(stdout);
    mcscene::Scene scene;
    scene.open(world);
    mcinventory::Inventory inventory;
    inventory.open(world);
    require(inventory.opened(), "inventory did not open");
    unchangedLive(initial, world.streamingInfo());
    viewPriority(world, true);

    // Prime just two fixed-timestep fractions, without a pump. The first actual
    // busy update must then advance an actual Scene tick even on a fast worker.
    scene.update(world, Dt);
    scene.update(world, Dt);
    const uint64_t initialTicks = scene.ticks();
    std::vector<uint32_t> pixels(size_t(Width) * Height);
    // Declared after World/Scene/Inventory: every failure joins/discards before
    // their destruction. No second WorldJob or generation context runs in parallel.
    ChunkPump pump;
    require(!pump.busy() && pump.poll() == State::Idle, "new pump is not Idle");
    Counters counts;
    const auto streamStart = Clock::now();
    while (world.streamingInfo().queued > 0) {
        const auto before = world.streamingInfo();
        const auto revision = world.revision();
        const auto requests = world.chunkRequests(Budget);
        require(!requests.empty(), "nonempty queue produced no chunk requests");
        require(pump.start(world, Budget), "pump rejected an idle start");
        if (!pump.busy()) {
            if (pump.poll() == State::Failed) workerFailure(pump);
            throw std::runtime_error("accepted nonempty start was not Running");
        }
        ++counts.batches;
        while (pump.busy()) {
            const auto frameStart = Clock::now();
            const uint64_t ticksBefore = scene.ticks();
            // This is intentionally normal live work BEFORE poll, not a wait loop
            // that freezes World while only collecting independent UI input.
            world.update(Dt, 0, 0, false, 0);
            ++counts.worldUpdates;
            scene.update(world, Dt);
            ++counts.sceneUpdates;
            worldrender::LightingSettings lighting;
            lighting.maxDistance = float(before.maxDistance);
            lighting.moonPhase = int((scene.ticks() / 24000) % 8);
            renderWorldFrame(pixels.data(), Width, Height, world, 0, 0, 3,
                             scene.daylight(), scene.timeOfDay(), lighting);
            ++counts.frameCalls;
            // Consume actual rendered pixels instead of merely counting a call.
            for (size_t at : {size_t(0), pixels.size() / 2, pixels.size() - 1})
                counts.pixelHash = counts.pixelHash * 1099511628211ULL + pixels[at];
            require(std::any_of(pixels.begin(), pixels.end(), [](uint32_t pixel) { return pixel != 0; }),
                    "valid busy render left the entire frame empty");
            const auto inventoryInfo = inventory.infoJson();
            ++counts.inventoryReads;
            const auto status = world.statusJson();
            ++counts.statusReads;
            require(!inventoryInfo.empty() && status.find("\"loaded\":true") != std::string::npos,
                    "busy inventory/status reads did not return live state");
            counts.readBytes += inventoryInfo.size() + status.size();
            // First busy frame also turns the camera on the still-uncommitted
            // queue. Turning must not invalidate worker-copied request epochs.
            if (counts.iterations == 0) viewPriority(world, false);
            // Save once per batch, not only on the very first busy frame: a cold
            // first batch can finish generating before that frame's poll, which
            // would leave the Running-overlap requirement trivially unmet.
            const bool savedThisFrame = counts.saves < uint64_t(counts.batches);
            if (savedThisFrame) {
                world.save();
                scene.save(world);
                inventory.save(world);
                ++counts.saves;
            }
            require(std::isfinite(world.playerX()) && std::isfinite(world.playerY()) &&
                    std::isfinite(world.playerZ()), "busy simulation produced a nonfinite player");
            unchangedLive(before, world.streamingInfo());
            require(world.revision() == revision, "terrain changed before owner poll");
            require(pump.busy(), "busy state changed without an owner poll/finish");
            ++counts.iterations;
            const auto pollStart = Clock::now();
            const auto state = pump.poll();
            counts.maxPollMs = std::max(counts.maxPollMs, elapsedMs(pollStart));
            counts.maxMainFrameMs = std::max(counts.maxMainFrameMs, elapsedMs(frameStart));
            if (state == State::Failed) workerFailure(pump);
            if (state == State::Running) {
                ++counts.runningIterations;
                counts.runningTicks += scene.ticks() - ticksBefore;
                if (savedThisFrame) ++counts.runningSaves;
                require(pump.busy(), "Running poll lost the busy lease");
            } else {
                require(state == State::Complete && !pump.busy(), "unexpected pump terminal state");
            }
        }
        const auto result = pump.finish(); // Already polled/joined; never a blocking generation wait.
        const auto after = world.streamingInfo();
        require(pump.error().empty(), "completed pump retained an error");
        require(result.count == int(requests.size()) && result.count > 0 && result.count <= Budget,
                "owner did not commit exactly the copied batch");
        require(after.loaded == before.loaded + result.count &&
                after.queued == before.queued - result.count &&
                after.totalGenerated == before.totalGenerated + uint64_t(result.count),
                "owner commit accounting mismatch");
        require(result.info.loaded == after.loaded && result.info.queued == after.queued &&
                result.info.totalGenerated == after.totalGenerated && result.info.target == Target,
                "pump result is not the post-commit live snapshot");
        for (const auto& request : requests)
            require(world.chunkLoaded((request.key % (WorldSize / 16)) * 16,
                                      (request.key / (WorldSize / 16)) * 16),
                    "a copied request was not committed by owner poll");
        counts.committed += result.count;
        require(counts.batches <= Target, "streaming batches made no bounded queue progress");
        std::printf("BATCH %d committed=%d loaded=%d queued=%d iterations=%llu running=%llu\n",
                    counts.batches, result.count, after.loaded, after.queued,
                    static_cast<unsigned long long>(counts.iterations),
                    static_cast<unsigned long long>(counts.runningIterations));
        std::fflush(stdout);
    }
    const auto full = world.streamingInfo();
    require(full.loaded == Target && full.target == Target && full.queued == 0 &&
            counts.committed == Target - initial.loaded, "render target was not completely loaded");
    require(counts.iterations > 0 && counts.worldUpdates == counts.iterations &&
            counts.sceneUpdates == counts.iterations && counts.frameCalls == counts.iterations &&
            counts.inventoryReads == counts.iterations && counts.statusReads == counts.iterations &&
            counts.saves > 0, "busy loop skipped real simulation/Scene/render/read/save work");
    // Running at the END of such a frame proves the work was not only performed
    // after a quick worker had already completed. Never silently skip this check.
    require(counts.runningIterations > 0 && counts.runningTicks > 0 && counts.runningSaves > 0,
            "no full live tick/render/read/save frame overlapped actual Running generation");
    require(scene.ticks() > initialTicks, "Scene ticks froze during streaming");
    pump.reset();
    require(!pump.busy() && pump.poll() == State::Idle && pump.error().empty(),
            "reset did not restore Idle");
    std::printf("LIVE STREAMING PASSED root=%s path=%s seconds=%.3f loaded=%d/%d "
                "batches=%d iterations=%llu running_iterations=%llu ticks=%llu running_ticks=%llu "
                "world_updates=%llu scene_updates=%llu frame_calls=%llu inventory_reads=%llu "
                "status_reads=%llu saves=%llu running_saves=%llu read_bytes=%llu pixel_hash=%llu "
                "max_mainframe_ms=%.3f max_poll_ms=%.3f\n",
                root, world.path().c_str(), elapsedMs(streamStart) / 1000, full.loaded, full.target,
                counts.batches, static_cast<unsigned long long>(counts.iterations),
                static_cast<unsigned long long>(counts.runningIterations),
                static_cast<unsigned long long>(scene.ticks() - initialTicks),
                static_cast<unsigned long long>(counts.runningTicks),
                static_cast<unsigned long long>(counts.worldUpdates),
                static_cast<unsigned long long>(counts.sceneUpdates),
                static_cast<unsigned long long>(counts.frameCalls),
                static_cast<unsigned long long>(counts.inventoryReads),
                static_cast<unsigned long long>(counts.statusReads),
                static_cast<unsigned long long>(counts.saves),
                static_cast<unsigned long long>(counts.runningSaves),
                static_cast<unsigned long long>(counts.readBytes),
                static_cast<unsigned long long>(counts.pixelHash),
                counts.maxMainFrameMs, counts.maxPollMs);
    std::fflush(stdout);
}
} // namespace

int main() {
    char root[] = "/tmp/minecraft-live-probe.XXXXXX";
    if (!::mkdtemp(root)) {
        std::fprintf(stderr, "FAIL: mkdtemp %s: %s\n", root, std::strerror(errno));
        return 1;
    }
    try {
        liveProbe(root);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL root=%s: %s\n", root, error.what());
    } catch (...) {
        std::fprintf(stderr, "FAIL root=%s: non-standard exception\n", root);
    }
    return 1;
}
