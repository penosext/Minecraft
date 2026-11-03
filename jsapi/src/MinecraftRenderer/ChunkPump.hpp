#pragma once

#include "World.hpp"

#include <atomic>
#include <exception>
#include <memory>
#include <pthread.h>
#include <string>
#include <vector>

namespace mcworld {

// SDK/QuickJS-independent background generation. Every public method, including
// destruction, belongs to one owner/main thread. Live World remains usable while
// busy: start snapshots value requests; only poll/finish ever commit to World.
//
// The worker owns an independent generation context, reused exclusively across
// steps. It never accesses World, its OriginalWorld, edits, render pointers, Scene,
// QuickJS or callbacks. No global exclusive-world lease is necessary.
//
// World is borrowed ONLY for a later main-thread commit. Before destroying it,
// call finish() or reset(); destruction/reset join and discard without touching
// World, so destroying World first is safe if the pump is then only reset/destroyed.
class ChunkPump {
public:
    enum class State { Idle, Running, Complete, Failed };
    struct Result {
        int count = 0; // Chunks actually committed, not generated/stale results.
        StreamingInfo info {}; // Owner-thread post-commit value snapshot.
    };

    ChunkPump();
    ~ChunkPump(); // Joins and discards. Never detaches or commits.
    ChunkPump(const ChunkPump&) = delete;
    ChunkPump& operator=(const ChunkPump&) = delete;
    ChunkPump(ChunkPump&&) = delete;
    ChunkPump& operator=(ChunkPump&&) = delete;

    // false only while an earlier step has not been joined. Budgets are bounded
    // by the current target queue (<=49). Invalid input/launch/commit errors use
    // the same Failed/error/finish channel, never disable access to live World.
    bool start(World& world, int maxChunks = 1);
    bool busy() const noexcept;
    // Nonblocking try-join includes TLS destruction, not just worker-body done.
    // Commit exceptions are caught, leave that chunk queued, and report Failed.
    State poll();
    // Blocking join + main-thread commit. Repeated terminal calls are stable.
    Result finish();
    std::string error() const;
    // Blocking join/discard; clear errors/results/borrowed World and become Idle.
    // Keeps the isolated context; next request reconfigures it if necessary.
    void reset();

private:
    static void* entry(void* self) noexcept;
    void run() noexcept;
    bool join(bool wait);
    void joined();

    pthread_t worker_ {};
    bool joinable_ = false;
    std::atomic<bool> done_ {false};
    World* world_ = nullptr; // Owner thread ONLY; never read by run()/entry().
    std::vector<ChunkRequest> requests_;
    std::vector<GeneratedChunk> generated_;
    std::unique_ptr<OriginalWorld> context_;
    Result result_ {};
    std::exception_ptr failure_;
    State state_ = State::Idle;
};

} // namespace mcworld
