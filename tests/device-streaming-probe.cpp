#include "WorldJob.hpp"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <unistd.h>

using Clock = std::chrono::steady_clock;
std::unique_ptr<mcworld::WorldSession> await(mcworld::WorldJob& job, const char* label)
{
    const auto start = Clock::now();
    std::string previous;
    double maxPollMs = 0;
    for (;;) {
        const auto before = Clock::now();
        const auto status = job.pollJson();
        const double pollMs = std::chrono::duration<double,std::milli>(Clock::now()-before).count();
        if (pollMs > maxPollMs) maxPollMs = pollMs;
        if (status != previous) { std::printf("%s %s\n",label,status.c_str()); std::fflush(stdout); previous=status; }
        if (job.ready()) {
            auto result = job.takeResult();
            if (!result) throw std::runtime_error("Ready job has no result");
            const auto stream = result->world.streamingInfo();
            if (!stream.enabled || stream.radius != 1 || stream.loaded != 9 || stream.target != 9 || stream.queued != 0)
                throw std::runtime_error("Initial target is not exactly nine ready chunks");
            const double seconds = std::chrono::duration<double>(Clock::now()-start).count();
            std::printf("%s PASSED seconds=%.3f max_poll_ms=%.3f %s\n",label,seconds,maxPollMs,result->world.streamingInfoJson().c_str());
            std::fflush(stdout);
            return result;
        }
        if (!job.active()) throw std::runtime_error(status);
        if (Clock::now()-start > std::chrono::seconds(90)) { job.cancel(); throw std::runtime_error("Job timed out"); }
        usleep(100000);
    }
}
int main()
{
    try {
        char root[] = "/tmp/minecraft-stream-probe.XXXXXX";
        if (!mkdtemp(root)) throw std::runtime_error("Cannot make independent test root");
        mcworld::WorldJob create(root);
        if (!create.startCreate(12345,5120,1)) throw std::runtime_error("Cannot start create");
        auto first = await(create,"CREATE");
        const auto path = first->world.path();
        const auto generated = first->world.streamingInfo().totalGenerated;
        first->world.beginStreaming(2);
        if (first->world.streamingInfo().target != 25) throw std::runtime_error("Radius two target mismatch");
        first->world.beginStreaming(1);
        if (first->world.streamingInfo().target != 9 || first->world.streamingInfo().totalGenerated != generated)
            throw std::runtime_error("Changing radius generated unrequested chunks synchronously");
        first->world.save(); first->scene.save(first->world); first->inventory.save(first->world);
        first.reset();
        mcworld::WorldJob open(root);
        if (!open.startOpen(path,1)) throw std::runtime_error("Cannot start open");
        auto restored = await(open,"OPEN");
        std::printf("DEVICE STREAMING VALIDATION PASSED root=%s path=%s\n",root,restored->world.path().c_str());
        return 0;
    } catch (const std::exception& error) { std::fprintf(stderr,"FAIL: %s\n",error.what()); return 1; }
}
