// Native integration regression; link renderer_runtime + world_runtime/modules.
#ifdef NDEBUG
#undef NDEBUG
#endif
#define main asyncWorldHelpersMain
#include "world-test.cpp"
#undef main
#include "ChunkPump.hpp"
#include "OriginalWorld.hpp"
#include "WorldRenderer.hpp"
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

namespace {
using mcworld::ChunkPump;
using mcworld::ChunkRequest;
using mcworld::GeneratedChunk;
using mcworld::OriginalWorld;
using State = ChunkPump::State;
using Clock = std::chrono::steady_clock;
constexpr int Chunks = 864 / 16;
size_t at(int x, int y, int z) { return (size_t(y) * 16 + z) * 16 + x; }

size_t position(const std::vector<ChunkRequest>& requests, int key) {
    for (size_t i = 0; i < requests.size(); ++i) if (requests[i].key == key) return i;
    assert(false); return requests.size();
}

void prioritiesAndEpochs() {
    OriginalWorld world(12345, 864, false, false);
    world.beginStreaming(1); world.setStreamingCenter(440.5f, 440.5f);
    const auto before = world.streamingInfo();
    const int key = 27 * Chunks + 27;
    const auto revision = world.streamingRevision();
    world.setStreamingView(0, 0, 1);
    const auto front = world.chunkRequests(999999);
    assert(front.size() == 9 && front.front().key == key);
    assert(position(front, key + Chunks) < position(front, key - Chunks));
    world.setStreamingView(180, 0, 1);
    const auto back = world.chunkRequests(999999);
    assert(back.front().key == key);
    assert(position(back, key - Chunks) < position(back, key + Chunks));
    assert(front.front().epoch == back.front().epoch);
    assert(world.streamingRevision() == revision);
    assert(world.streamingInfo().totalGenerated == before.totalGenerated);
    // No view update reads unknown FINAL, even for vertical/wide views.
    for (float pitch : {-90.f, -60.f, 60.f, 90.f}) {
        world.setStreamingView(90, pitch, 2.4f);
        assert(world.chunkRequests(1).front().key == key);
    }
    rejects([&] { world.setStreamingView(NAN, 0, 1); });
    rejects([&] { world.setStreamingView(0, INFINITY, 1); });
    rejects([&] { world.setStreamingView(0, 0, 0); });
    rejects([&] { world.chunkRequests(-1); });
    assert(world.chunkRequests(0).empty());
    std::unique_ptr<OriginalWorld> context;
    auto old = OriginalWorld::generateChunk(front.front(), context);
    world.setStreamingCenter(456.5f, 440.5f); // old key still in target, but stale epoch
    assert(world.chunkRequests(1).front().epoch != old.request.epoch);
    assert(!world.commitChunk(std::move(old)) && world.streamingInfo().loaded == 0);
    auto request = world.chunkRequests(1).front();
    auto result = OriginalWorld::generateChunk(request, context);
    OriginalWorld other(12345, 864, false, false);
    other.beginStreaming(1); other.setStreamingCenter(456.5f, 440.5f);
    assert(other.chunkRequests(1).front().epoch != request.epoch);
    assert(!other.commitChunk(std::move(result)));
    result = OriginalWorld::generateChunk(request, context);
    world.endStreaming(); world.beginStreaming(1);
    assert(!world.commitChunk(std::move(result)));
    world.setRenderRadius(3);
    assert(world.chunkRequests(999999).size() == 49);
}

void latestOverlayAndCorruption() {
    OriginalWorld world(12345, 864, false, false);
    world.beginStreaming(1); world.setStreamingCenter(440.5f, 440.5f);
    const auto request = world.chunkRequests(1).front();
    std::unique_ptr<OriginalWorld> context;
    auto result = OriginalWorld::generateChunk(request, context);
    const auto baseline = result.baseline;
    const auto biomes = result.biomes;
    const auto volume = world.renderVolume(440.5f, 440.5f);
    OriginalWorld::ModifiedChunk changes;
    changes.biomes = biomes; changes.edits[uint32_t(at(8, 200, 8))] = blockId("stone");
    changes.persisted = true; changes.persistedHash = 0x12345678;
    world.restoreChunk(request.key, changes, true); // latest overlay arrived AFTER request
    assert(world.chunkRequests(1).front().epoch == request.epoch);
    assert(world.commitChunk(std::move(result)));
    assert(world.blockAt(440, 200, 440) == blockId("stone"));
    const auto& committed = world.modifiedChunks().at(request.key);
    assert(committed.baselineValidated && committed.persisted && !committed.dirty);
    assert(committed.persistedHash == 0x12345678); // no save-baseline upgrade
    assert(world.streamingInfo().deferred == 0);
    const auto published = world.renderVolume(440.5f, 440.5f);
    assert(published.data[(size_t(200) * 48 + 24) * 48 + 24] == blockId("stone"));
    (void)volume; // Old pointers expire at commit, never during worker execution.

    // A synchronous load/newer edit wins over an already generated result.
    const auto another = world.chunkRequests(1).front();
    auto duplicate = OriginalWorld::generateChunk(another, context);
    const int x = (another.key % Chunks) * 16 + 8, z = (another.key / Chunks) * 16 + 8;
    assert(world.loadChunk(x, z));
    assert(world.setBlock(x, 200, z, blockId("stone")));
    assert(world.setBlock(x, 200, z, blockId("glass")));
    assert(!world.commitChunk(std::move(duplicate)));
    assert(world.blockAt(x, 200, z) == blockId("glass"));

    for (int mutation = 0; mutation < 4; ++mutation) {
        OriginalWorld corrupt(12345, 864, false, false);
        corrupt.beginStreaming(1); corrupt.setStreamingCenter(440.5f, 440.5f);
        const auto r = corrupt.chunkRequests(1).front();
        auto generated = OriginalWorld::generateChunk(r, context);
        OriginalWorld::ModifiedChunk bad; bad.biomes = biomes;
        if (mutation == 0) bad.edits[uint32_t(at(8, 200, 8))] = 0; // redundant air removal
        if (mutation == 1) bad.edits[uint32_t(at(8, 0, 8))] = 0; // bedrock
        if (mutation == 2) { bad.formatVersion = 1; bad.edits[uint32_t(at(8, 200, 8))] = 0; }
        if (mutation == 3) bad.edits[uint32_t(at(8, 1, 8))] = baseline[at(8, 1, 8)];
        corrupt.restoreChunk(r.key, bad, true);
        const auto old = corrupt.renderVolume(440.5f, 440.5f);
        const auto rev = corrupt.streamingRevision();
        rejects([&] { corrupt.commitChunk(std::move(generated)); });
        const auto info = corrupt.streamingInfo();
        assert(info.deferred == 1 && info.loaded == 0 && info.queued == 9 && info.totalGenerated == 0);
        assert(corrupt.streamingRevision() == rev && corrupt.renderVolume(440.5f, 440.5f).data == old.data);
        assert(!corrupt.modifiedChunks().at(r.key).baselineValidated);
    }
}

void contextsAcrossThreads() {
    // A context can migrate between joined worker threads. Its internal callbacks
    // still point to its own Impl, not old live worlds or expired TLS pools.
    std::unique_ptr<OriginalWorld> context;
    GeneratedChunk previous;
    for (int step = 0; step < 6; ++step) {
        auto live = std::make_unique<OriginalWorld>(uint32_t(12345 + step / 2), 864, step >= 2, step >= 4);
        live->beginStreaming(1); live->setStreamingCenter(440.5f, 440.5f);
        auto request = live->chunkRequests(1).front();
        GeneratedChunk generated;
        std::exception_ptr failure;
        std::thread worker([&] {
            try { generated = OriginalWorld::generateChunk(request, context); }
            catch (...) { failure = std::current_exception(); }
        });
        live.reset(); // no dependency on live state even during initialization
        worker.join();
        if (failure) std::rethrow_exception(failure);
        assert(generated.baseline.size() == OriginalWorld::ChunkCells);
        assert(context->modifiedChunks().empty() && context->streamingInfo().resident == 0);
        if (step % 2) assert(generated.baseline == previous.baseline && generated.biomes == previous.biomes);
        previous = std::move(generated);
    }
}

void liveWorldDuringPump() {
    Temp root;
    World world; world.beginStreaming(3);
    world.create(12345, root.path, true, 864, true, true);
    // Keep edits in the loaded safety chunk; no worker result may overwrite them.
    const int x = int(world.playerX()), z = int(world.playerZ());
    const int y = int(world.playerY()) - 1;
    const auto firstHit = world.pick(0, -90);
    assert(firstHit.hit && firstHit.y == y);
    world.setStreamingView(0, 0, 1.5f);
    ChunkPump pump;
    const auto before = world.streamingInfo();
    const auto oldVolume = world.renderVolume();
    const auto oldRevision = world.revision();
    const auto oldCell = oldVolume.data[0];
    assert(pump.start(world, 49));
    assert(world.mine(0, -90));
    assert(world.blockAt(x, y, z) == 0);
    int frames = 0;
    std::vector<uint32_t> pixels(48 * 32, 0);
    const auto started = Clock::now();
    // No poll until after these frames, so all live state/result publication is
    // stable. The expensive v3 batch is genuinely concurrent with initial frames.
    for (int i = 0; i < 3; ++i) {
        assert(pump.busy());
        world.update(.01f, 0, 0, false, 0);
        world.statusJson(); world.biomeInfo(); world.biomeAt(80, 80);
        world.blockAt(x, y, z); world.renderVolume();
        world.setStreamingView(float(i * 90), 15, 1.5f);
        renderWorldFrame(pixels.data(), 48, 32, world, float(i * 90), 15, 4);
        world.save(); // no join, no immutable generation baseline upgrade
        assert(world.blockAt(x, y, z) == 0);
        assert(world.revision() == oldRevision + 1);
        assert(world.renderVolume().data == oldVolume.data && oldVolume.data[0] == oldCell);
        assert(world.streamingInfo().totalGenerated == before.totalGenerated);
        ++frames;
    }
    assert(frames == 3 && std::any_of(pixels.begin(), pixels.end(), [](uint32_t p) { return p != 0; }));
    while (pump.poll() == State::Running) std::this_thread::yield();
    assert(pump.poll() == State::Complete);
    const auto result = pump.finish();
    assert(result.count == before.queued && result.info.queued == 0);
    assert(world.blockAt(x, y, z) == 0);
    const auto snapshot = result.info;
    world.setRenderRadius(1);
    assert(pump.finish().info.target == snapshot.target && pump.finish().count == result.count);
    world.save();
    World reopened; reopened.beginStreaming(1); reopened.open(world.path(), root.path);
    assert(reopened.blockAt(x, y, z) == 0);
    std::cout << "async live frames=" << frames << " elapsedMs="
              << std::chrono::duration<double, std::milli>(Clock::now() - started).count() << '\n';
}
}
int main() {
    prioritiesAndEpochs(); latestOverlayAndCorruption(); contextsAcrossThreads(); liveWorldDuringPump();
    std::cout << "async-chunk-test: view/epochs/latest overlays/corruption/context thread lifetime/live update+read+render+edit+save passed\n";
}
