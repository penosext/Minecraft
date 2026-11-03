#pragma once

#include "World.hpp"
#include "Scene.hpp"
#include "Inventory.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace mcworld {

// The worker owns all three objects until takeResult(). Never expose or render
// this World while a job is active; the old session must also stop world/scene
// rendering and simulation (the independent block preview may keep rendering).
struct WorldSession {
    World world;
    mcscene::Scene scene;
    mcinventory::Inventory inventory;
};

// SDK/QuickJS-independent, single-use asynchronous session loader. Start/poll/
// cancel/ready/takeResult belong to the owning/main thread. Progress snapshots
// are synchronized; the worker never accesses the caller's current session.
// Only one WorldJob worker may run in the process at a time, including across
// instances, because original generation/scene initialization has lazy globals.
class WorldJob {
public:
    // The flags are host-fixture overrides only; production keeps both true.
    explicit WorldJob(std::string root = "/tmp/mc", bool structures = true,
                      bool decoration = true);
    ~WorldJob(); // Requests cancellation and joins; an in-flight chunk can delay it.

    WorldJob(const WorldJob&) = delete;
    WorldJob& operator=(const WorldJob&) = delete;
    WorldJob(WorldJob&&) = delete;
    WorldJob& operator=(WorldJob&&) = delete;

    // false means not started (this instance was already used, or another job
    // still owns the worker lease). Accepted operations report errors via poll.
    // Radius 1 requests the real clipped 3x3 chunk neighborhood, NOT 49 chunks.
    bool startCreate(uint32_t seed, int size = 5120, int radius = 1);
    bool startOpen(const std::string& path, int radius = 1);

    // Flat JSON: state, stage, completed, total, progress (0..100), loadedChunks,
    // totalChunks, error; worldInfo is included after successful load/create.
    // total=0 means not yet known (spawn still reports its real completed count).
    // States: idle/running/cancelling/cancelled/failed/ready/taken. A cancelled
    // result is never applied; cancel on an already published terminal is a no-op.
    // Cancellation/failure after creation preserves the world; worldInfo.path
    // identifies it. No caller-owned World is inspected here. Normal job errors
    // never throw from pollJson(). 100 is reserved exclusively for ready.
    std::string pollJson() const;
    void cancel() noexcept; // Immediate request: cancelling -> cancelled, never ready.
    bool active() const noexcept; // queued/running/cancelling, until the worker exits.
    bool ready() const noexcept;  // true only after the worker has been joined.
    // Returns null unless ready(); never waits for running generation. On success
    // transfers exclusive ownership once, and state becomes "taken".
    std::unique_ptr<WorldSession> takeResult();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mcworld
