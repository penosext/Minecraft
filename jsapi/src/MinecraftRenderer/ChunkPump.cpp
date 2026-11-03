#ifndef _GNU_SOURCE
#define _GNU_SOURCE // pthread_tryjoin_np: poll must not wait for TLS destructors.
#endif
#include "ChunkPump.hpp"
#include "OriginalWorld.hpp"

#include <cerrno>
#include <stdexcept>
#include <system_error>

namespace mcworld {

ChunkPump::ChunkPump() = default;
ChunkPump::~ChunkPump() { reset(); }

bool ChunkPump::start(World& world, int maxChunks) {
    if (busy()) return false;
    result_ = Result {}; failure_ = nullptr;
    requests_.clear(); generated_.clear(); world_ = &world;
    done_.store(false, std::memory_order_relaxed);
    state_ = State::Running;
    try {
        // All live access, validation and request allocation is owner-thread only.
        if (!world.loaded()) throw std::runtime_error("ChunkPump: no world loaded");
        if (maxChunks < 0) throw std::invalid_argument("ChunkPump: negative chunk load budget");
        requests_ = world.chunkRequests(maxChunks);
        generated_.reserve(requests_.size());
        if (requests_.empty()) {
            done_.store(true, std::memory_order_release);
        } else {
            const int error = pthread_create(&worker_, nullptr, &ChunkPump::entry, this);
            if (error) throw std::system_error(error, std::generic_category(), "ChunkPump: launch");
            joinable_ = true;
        }
    } catch (...) {
        failure_ = std::current_exception();
        done_.store(true, std::memory_order_release);
    }
    return true;
}

bool ChunkPump::busy() const noexcept { return state_ == State::Running; }

void* ChunkPump::entry(void* self) noexcept {
    static_cast<ChunkPump*>(self)->run();
    return nullptr;
}
void ChunkPump::run() noexcept {
    try {
        for (const auto& request : requests_)
            generated_.push_back(OriginalWorld::generateChunk(request, context_));
    } catch (...) {
        failure_ = std::current_exception();
    }
    done_.store(true, std::memory_order_release);
}

bool ChunkPump::join(bool wait) {
    if (!joinable_) return true;
    const int error = wait ? pthread_join(worker_, nullptr) : pthread_tryjoin_np(worker_, nullptr);
    if (!wait && error == EBUSY) return false;
    if (error) throw std::system_error(error, std::generic_category(), "ChunkPump: join");
    joinable_ = false;
    return true;
}

void ChunkPump::joined() {
    // Called exactly once after the worker/TLS have terminated. Worker failure
    // discards the whole generated batch; commit failure may follow earlier
    // successful chunks, whose count remains accurate and state stays usable.
    try {
        if (!failure_) {
            for (auto& chunk : generated_)
                result_.count += world_->commitChunk(std::move(chunk)) ? 1 : 0;
        }
    } catch (...) {
        failure_ = std::current_exception();
    }
    try {
        if (world_ && world_->loaded()) result_.info = world_->streamingInfo();
    } catch (...) {
        if (!failure_) failure_ = std::current_exception();
    }
    generated_.clear(); requests_.clear(); world_ = nullptr;
    state_ = failure_ ? State::Failed : State::Complete;
}

ChunkPump::State ChunkPump::poll() {
    if (state_ != State::Running || !done_.load(std::memory_order_acquire)) return state_;
    if (join(false)) joined();
    return state_;
}

ChunkPump::Result ChunkPump::finish() {
    if (state_ == State::Idle) throw std::logic_error("ChunkPump: no step started");
    if (state_ == State::Running) {
        join(true);
        joined();
    }
    if (failure_) std::rethrow_exception(failure_);
    return result_;
}

void ChunkPump::reset() {
    // No live World reads, including on failure/destruction after World's death.
    join(true);
    requests_.clear(); generated_.clear(); world_ = nullptr;
    result_ = Result {}; failure_ = nullptr;
    done_.store(false, std::memory_order_relaxed);
    state_ = State::Idle;
}

std::string ChunkPump::error() const {
    // failure_ belongs to the worker while Running; never inspect it then.
    if (state_ != State::Failed || !failure_) return {};
    try {
        std::rethrow_exception(failure_);
    } catch (const std::exception& e) {
        return e.what();
    } catch (...) {
        return "ChunkPump: unknown worker error";
    }
}

} // namespace mcworld
