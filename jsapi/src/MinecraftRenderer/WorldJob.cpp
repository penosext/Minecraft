#ifndef _GNU_SOURCE
#define _GNU_SOURCE // pthread_tryjoin_np: ready must never wait for chunk generation.
#endif
#include "WorldJob.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <locale>
#include <mutex>
#include <pthread.h>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace mcworld {
namespace {
std::atomic<bool> workerOwned {false};
struct Cancelled {};

enum class State { Idle, Running, Cancelling, Cancelled, Failed, Ready, Taken };
const char* stateName(State state) {
    switch (state) {
    case State::Idle: return "idle";
    case State::Running: return "running";
    case State::Cancelling: return "cancelling";
    case State::Cancelled: return "cancelled";
    case State::Failed: return "failed";
    case State::Ready: return "ready";
    case State::Taken: return "taken";
    }
    return "failed";
}
std::string quote(const std::string& value) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string result = "\"";
    for (unsigned char c : value) {
        switch (c) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\b': result += "\\b"; break;
        case '\f': result += "\\f"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (c < 0x20) { result += "\\u00"; result += hex[c >> 4]; result += hex[c & 15]; }
            else result += char(c);
        }
    }
    return result + '"';
}
} // namespace

struct WorldJob::Impl {
    const std::string root;
    const bool structures, decoration;
    mutable std::mutex mutex;
    std::atomic<bool> cancelRequested {false};
    // This is only a hint: tryjoin, NOT done, proves actual thread termination
    // (including original-biome thread_local cache destructors).
    std::atomic<bool> done {false};
    pthread_t worker {};
    bool joinable = false, lease = false, started = false;
    bool creating = false, succeeded = false;
    uint32_t seed = 0;
    int size = 5120, radius = 1;
    std::string openPath;
    State state = State::Idle;
    std::string stage = "idle", error, worldInfo;
    int completed = 0, total = 0, progress = 0, loadedChunks = 0, totalChunks = 0;
    std::unique_ptr<WorldSession> result;

    Impl(std::string root_, bool structures_, bool decoration_)
        : root(std::move(root_)), structures(structures_), decoration(decoration_) {}

    void checkpoint() const {
        if (cancelRequested.load(std::memory_order_relaxed)) throw Cancelled {};
    }
    void phase(const std::string& name, int count, int target, int percent) {
        std::lock_guard<std::mutex> lock(mutex);
        if (cancelRequested.load(std::memory_order_relaxed)) throw Cancelled {};
        stage = name;
        total = std::max(0, target);
        // total=0 means not yet known, not zero work. Preserve actual spawn
        // chunks done while withholding percentage estimates until it is known.
        completed = total ? std::clamp(count, 0, total) : std::max(0, count);
        progress = std::max(progress, std::clamp(percent, 0, 99));
    }
    void creationProgress(const CreateProgress& value) {
        checkpoint();
        // Spawn reports actual chunks and total=0 until the safe spawn is
        // found. Do not invent a budget or estimated successful-spawn distance.
        const int target = std::max(0, value.total);
        const int count = target ? std::clamp(value.done, 0, target) : std::max(0, value.done);
        int percent = 0;
        if (value.stage == "spawn") percent = target ? 20 * count / target : 0;
        else if (value.stage == "initialsave") percent = 20 + (target ? 9 * count / target : 0);
        else if (value.stage == "post") percent = 29 + (target ? count / target : 0);
        phase(value.stage, count, target, percent);
    }
    StreamingInfo chunks(World& world) {
        checkpoint();
        const auto info = world.streamingInfo();
        std::lock_guard<std::mutex> lock(mutex);
        if (cancelRequested.load(std::memory_order_relaxed)) throw Cancelled {};
        loadedChunks = std::max(0, info.loaded);
        totalChunks = std::max(0, info.target);
        stage = "chunks";
        completed = loadedChunks;
        total = totalChunks;
        const int percent = totalChunks ? 30 + int(55LL * loadedChunks / totalChunks) : 85;
        progress = std::max(progress, std::clamp(percent, 30, 85));
        return info;
    }
    void rememberWorld(const World& world) {
        // No blockCount/renderVolume reads here: even diagnostics must not
        // accidentally generate the legacy 96x96 / 49-chunk neighborhood.
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << "{\"loaded\":true,\"path\":" << quote(world.path())
            << ",\"seed\":" << world.seed() << ",\"size\":[" << world.width()
            << ',' << world.height() << ',' << world.depth() << "]}";
        std::lock_guard<std::mutex> lock(mutex);
        worldInfo = out.str();
    }
    void run() noexcept {
        std::unique_ptr<WorldSession> session;
        try {
            checkpoint();
            session = std::make_unique<WorldSession>();
            // Validate the radius with World's own API before creating files.
            session->world.beginStreaming(radius);
            checkpoint();
            if (creating) {
                phase("spawn", 0, 0, 0);
                session->world.create(seed, root, true, size, structures, decoration,
                    [this](const CreateProgress& value) {
                        const bool committed = value.stage == "post" ||
                            (value.stage == "initialsave" && value.total > 0 && value.done >= value.total);
                        if (committed) {
                            // Saving has completed. Do not throw here: return
                            // first, record its path, then observe cancellation.
                            if (!cancelRequested.load(std::memory_order_relaxed)) {
                                try { creationProgress(value); } catch (const Cancelled&) {}
                            }
                        } else creationProgress(value);
                    });
            } else {
                phase("open", 0, 1, 0);
                session->world.open(openPath, root);
            }
            rememberWorld(session->world);
            checkpoint();
            phase(creating ? "created" : "open", 1, 1, 30);
            auto info = chunks(session->world);
            while (info.queued > 0) {
                checkpoint();
                const int processed = session->world.processChunkLoads(1);
                checkpoint();
                const auto next = chunks(session->world);
                if (processed <= 0 && next.queued >= info.queued && next.loaded <= info.loaded)
                    throw std::runtime_error("WorldJob: chunk queue made no progress");
                info = next;
            }
            if (info.loaded < info.target) throw std::runtime_error("WorldJob: incomplete initial chunk target");
            phase("scene", 0, 1, 85);
            session->scene.open(session->world);
            checkpoint();
            phase("scene", 1, 1, 90);
            phase("inventory", 0, 1, 90);
            session->inventory.open(session->world);
            checkpoint();
            phase("inventory", 1, 1, 95);
            // Open both validated companions before ANY migration/save. Their
            // save methods reject unknown/external changes instead of clobbering.
            // World::save preserves the immutable old generator and metadata.
            phase("finalsave", 0, 3, 95);
            session->world.save();
            checkpoint();
            phase("finalsave", 1, 3, 96);
            session->scene.save(session->world);
            checkpoint();
            phase("finalsave", 2, 3, 97);
            session->inventory.save(session->world);
            checkpoint();
            phase("finalsave", 3, 3, 99);
            const auto status = session->world.statusJson();
            checkpoint();
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (cancelRequested.load(std::memory_order_relaxed)) throw Cancelled {};
                worldInfo = status;
                result = std::move(session);
                succeeded = true;
                // State/progress become ready/100 only when tryjoin succeeds.
                stage = "finishing";
            }
        } catch (const Cancelled&) {
            // The main thread publishes cancelled only after the worker exits.
            std::lock_guard<std::mutex> lock(mutex);
            state = State::Cancelling;
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(mutex);
            if (cancelRequested.load(std::memory_order_relaxed)) state = State::Cancelling;
            else { state = State::Failed; error = e.what(); }
        } catch (...) {
            std::lock_guard<std::mutex> lock(mutex);
            if (cancelRequested.load(std::memory_order_relaxed)) state = State::Cancelling;
            else { state = State::Failed; error = "WorldJob: unknown worker error"; }
        }
        // Destroy cancelled/failed sessions on the worker, before advertising
        // completion. WorldJob NEVER removes any world directory.
        session.reset();
        done.store(true, std::memory_order_release);
    }
    static void* entry(void* pointer) noexcept {
        static_cast<Impl*>(pointer)->run();
        return nullptr;
    }
    void releaseLease() noexcept {
        if (lease) { workerOwned.store(false, std::memory_order_release); lease = false; }
    }
    void reaped() {
        joinable = false;
        releaseLease();
        std::lock_guard<std::mutex> lock(mutex);
        if (cancelRequested.load(std::memory_order_relaxed)) {
            result.reset();
            succeeded = false;
            state = State::Cancelled;
            stage = "cancelled";
        } else if (succeeded) {
            state = State::Ready;
            stage = "ready";
            completed = total = 1;
            progress = 100;
        }
    }
    void reap() noexcept {
        // Owner-thread only: the mutex is never held while trying to join.
        if (!joinable || !done.load(std::memory_order_acquire)) return;
        if (::pthread_tryjoin_np(worker, nullptr) == 0) reaped();
    }
    bool start(bool create, uint32_t nextSeed, int nextSize, int nextRadius, const std::string& path) {
        if (started) return false; // Terminal results/errors must not be overwritten.
        bool available = false;
        if (!workerOwned.compare_exchange_strong(available, true, std::memory_order_acq_rel)) return false;
        lease = true;
        try {
            openPath = path;
            creating = create; seed = nextSeed; size = nextSize; radius = nextRadius;
            started = true;
            state = State::Running;
            stage = "queued";
            const int code = ::pthread_create(&worker, nullptr, &Impl::entry, this);
            if (code != 0) {
                state = State::Failed;
                stage = "queued";
                error = std::string("WorldJob: start worker: ") + std::strerror(code);
                done.store(true, std::memory_order_release);
                releaseLease();
            } else joinable = true;
        } catch (...) { releaseLease(); throw; }
        return true;
    }
};

WorldJob::WorldJob(std::string root, bool structures, bool decoration)
    : impl_(std::make_unique<Impl>(std::move(root), structures, decoration)) {}
WorldJob::~WorldJob() {
    cancel();
    if (impl_->joinable) {
        // No detach, no polling loop, and no blocking while holding progress mutex.
        ::pthread_join(impl_->worker, nullptr);
        impl_->joinable = false;
        impl_->releaseLease();
    }
}
bool WorldJob::startCreate(uint32_t seed, int size, int radius) {
    return impl_->start(true, seed, size, radius, {});
}
bool WorldJob::startOpen(const std::string& path, int radius) {
    return impl_->start(false, 0, 5120, radius, path);
}
void WorldJob::cancel() noexcept {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->state != State::Running && impl_->state != State::Cancelling) return;
    impl_->cancelRequested.store(true, std::memory_order_relaxed);
    impl_->state = State::Cancelling;
    // No tryjoin here: cancellation is an immediate request, visible to the UI.
    // Even a finished-but-not-yet-reaped worker is disarmed. Already published
    // ready/failed/cancelled/taken states are terminal and are not rewritten.
}
bool WorldJob::active() const noexcept {
    impl_->reap();
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->joinable || impl_->state == State::Running || impl_->state == State::Cancelling;
}
bool WorldJob::ready() const noexcept {
    impl_->reap();
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->state == State::Ready;
}
std::unique_ptr<WorldSession> WorldJob::takeResult() {
    impl_->reap();
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->state != State::Ready) return nullptr;
    impl_->state = State::Taken;
    impl_->stage = "taken";
    return std::move(impl_->result);
}
std::string WorldJob::pollJson() const {
    impl_->reap();
    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "{\"state\":" << quote(stateName(impl_->state)) << ",\"stage\":" << quote(impl_->stage)
        << ",\"completed\":" << impl_->completed << ",\"total\":" << impl_->total
        << ",\"progress\":" << impl_->progress << ",\"loadedChunks\":" << impl_->loadedChunks
        << ",\"totalChunks\":" << impl_->totalChunks << ",\"error\":" << quote(impl_->error);
    if (!impl_->worldInfo.empty()) out << ",\"worldInfo\":" << impl_->worldInfo;
    out << '}';
    return out.str();
}
} // namespace mcworld
