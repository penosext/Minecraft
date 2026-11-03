// Standalone C++17 host test; no SDK, QuickJS, network, or CMake required.
// Compile with ChunkPump.cpp, World.cpp, OriginalWorld.cpp and generated catalog;
// link original_decoration, original_structures, original_terrain,
// original_biomes, original_core and -pthread (same as original-world-test).
#ifdef NDEBUG
#undef NDEBUG // Test operations inside assertions must execute in release builds.
#endif
#include "ChunkPump.hpp"

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>

namespace {
using mcworld::ChunkPump;
using mcworld::StreamingInfo;
using mcworld::World;
using State = ChunkPump::State;

static_assert(!std::is_copy_constructible<ChunkPump>::value, "worker must have one owner");
static_assert(!std::is_move_constructible<ChunkPump>::value, "worker captures its owner");

struct Temp {
    std::string path;
    Temp() {
        char pattern[] = "/tmp/mc-chunk-pump-XXXXXX";
        const char* value = ::mkdtemp(pattern);
        assert(value);
        path = value;
    }
    ~Temp() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

template<class Exception, class Function>
void rejects(Function function) {
    bool threw = false;
    try { function(); }
    catch (const Exception&) { threw = true; }
    assert(threw);
}

// Do not call finish immediately after start: exercise the UI's short polling
// path. Live World access is legal throughout (exercised by async-chunk-test).
State wait(ChunkPump& pump) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    for (;;) {
        const State state = pump.poll();
        if (state != State::Running) {
            assert(!pump.busy());
            return state;
        }
        assert(pump.busy());
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

void matches(const StreamingInfo& a, const StreamingInfo& b) {
    assert(a.enabled == b.enabled && a.radius == b.radius);
    assert(a.windowSize == b.windowSize && a.maxDistance == b.maxDistance);
    assert(a.loaded == b.loaded && a.queued == b.queued && a.target == b.target);
    assert(a.resident == b.resident && a.totalGenerated == b.totalGenerated);
    assert(a.centerChunkX == b.centerChunkX && a.centerChunkZ == b.centerChunkZ);
    assert(a.baseCached == b.baseCached && a.structureCached == b.structureCached);
    assert(a.deferred == b.deferred && a.lastLoadMs == b.lastLoadMs);
}

ChunkPump::Result completed(ChunkPump& pump, World& world,
                            const StreamingInfo& before, int budget) {
    assert(wait(pump) == State::Complete);
    assert(pump.poll() == State::Complete); // Stable, already joined.
    assert(pump.error().empty());
    const auto result = pump.finish(); // Only called after completion, no wait.
    matches(result.info, world.streamingInfo());
    matches(result.info, pump.finish().info); // Repeatable value snapshot.
    assert(result.count >= 0 && result.count <= budget);
    assert(result.info.loaded - before.loaded == result.count);
    assert(before.queued - result.info.queued == result.count);
    assert(result.info.totalGenerated - before.totalGenerated == unsigned(result.count));
    assert(std::isfinite(result.info.lastLoadMs) && result.info.lastLoadMs >= 0);
    return result;
}

void idleAndFailure() {
    // Failed starts remain Running until polled, with the same error channel.
    World closed;
    ChunkPump pump;
    assert(pump.poll() == State::Idle && !pump.busy() && pump.error().empty());
    rejects<std::logic_error>([&] { pump.finish(); });
    assert(pump.poll() == State::Idle);
    assert(pump.start(closed));
    assert(pump.busy());
    assert(!pump.start(closed)); // Cannot replace even a just-completed worker.
    assert(wait(pump) == State::Failed);
    const auto error = pump.error();
    assert(error.find("no world loaded") != std::string::npos);
    rejects<std::runtime_error>([&] { pump.finish(); });
    rejects<std::runtime_error>([&] { pump.finish(); });
    assert(pump.poll() == State::Failed && pump.error() == error);
    // A failed pump is reusable; a new start clears the old error.
    assert(pump.start(closed));
    assert(pump.error().empty());
    assert(wait(pump) == State::Failed);
    rejects<std::runtime_error>([&] { pump.finish(); });
}

void streamingSteps() {
    Temp root;
    auto world = std::make_unique<World>();
    world->beginStreaming(1);
    world->create(12345, root.path, true, 864, false, false);
    const auto initial = world->streamingInfo();
    assert(initial.enabled && initial.radius == 1 && initial.target == 9);
    assert(initial.loaded > 0 && initial.loaded < initial.target && initial.queued > 0);
    assert(initial.loaded + initial.queued == initial.target);

    ChunkPump pump;
    // Default budget really loads <=1, and rejects a second start until polled.
    assert(pump.start(*world));
    assert(pump.busy() && !pump.start(*world, 100));
    const auto first = completed(pump, *world, initial, 1);
    assert(first.count == 1 && first.info.queued > 0);

    // Negative budget propagates through the failure channel, preserving queue.
    const auto beforeInvalid = world->streamingInfo();
    assert(pump.start(*world, -1));
    assert(wait(pump) == State::Failed && !pump.error().empty());
    rejects<std::invalid_argument>([&] { pump.finish(); });
    matches(beforeInvalid, world->streamingInfo());

    // Zero is a successful no-op and clears the previous failure.
    assert(pump.start(*world, 0));
    assert(pump.error().empty());
    assert(completed(pump, *world, beforeInvalid, 0).count == 0);

    // Destruction joins and DISCARDS, never commits to the borrowed live World.
    const auto beforeDestructor = world->streamingInfo();
    {
        ChunkPump joining;
        assert(joining.start(*world));
        assert(joining.busy());
    }
    const auto afterDestructor = world->streamingInfo();
    matches(afterDestructor, beforeDestructor);
    assert(pump.start(*world));
    pump.reset();
    assert(pump.poll() == State::Idle && !pump.busy() && pump.error().empty());
    matches(world->streamingInfo(), beforeDestructor);

    // Reuse across frames; cached Result is a value, not a view of mutable World.
    int steps = 0;
    while (world->streamingInfo().queued > 0) {
        const auto before = world->streamingInfo();
        const int budget = steps++ == 0 ? 2 : 1;
        assert(pump.start(*world, budget));
        assert(completed(pump, *world, before, budget).count > 0);
        assert(steps <= 9);
    }
    // The first snapshot remains intact after later steps.
    assert(first.info.loaded == initial.loaded + 1 && first.info.queued == initial.queued - 1);
    const auto full = world->streamingInfo();
    assert(full.loaded == 9 && full.target == 9 && full.queued == 0);
    assert(pump.start(*world));
    assert(completed(pump, *world, full, 1).count == 0);

    // Explicitly release World while the pump object still exists, but ONLY
    // after poll has joined; its later destructor must not retain/dereference it.
    world.reset();
    assert(!pump.busy() && pump.poll() == State::Complete);
    matches(full, pump.finish().info);

    // A running pump has no live dependency for generation or destruction.
    auto dying = std::make_unique<World>();
    dying->beginStreaming(1); dying->create(12346, root.path, true, 864, false, false);
    auto discarding = std::make_unique<ChunkPump>();
    assert(discarding->start(*dying));
    dying.reset();
    discarding.reset(); // joins/discards, must not dereference the dead World.
}
} // namespace

int main() {
    idleAndFailure();
    streamingSteps();
    std::puts("chunk-pump-test: polling, budgets, snapshots, failures, reuse, double-start, destructor join and World lifetime passed");
}
