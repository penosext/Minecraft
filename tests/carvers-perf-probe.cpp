// Chunk generation and lighting cost, v3 (no carvers) vs v4 (caves/canyons/ores).
// Not a ctest: prints timings. Usage: carvers_perf_probe [seed] [radius] [size]
#include "OriginalWorld.hpp"
#include "WorldRenderer.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using mcworld::OriginalWorld;
using Clock = std::chrono::steady_clock;
namespace {
double ms(Clock::time_point a) { return std::chrono::duration<double, std::milli>(Clock::now() - a).count(); }
void generation(uint32_t seed, int size, int radius, bool carvers) {
    // Same request/isolated-context path the ChunkPump worker uses, in the
    // live queue order (center first, then by distance).
    OriginalWorld live(seed, size, true, true, carvers);
    live.setChunkRanges(radius, radius); live.beginStreaming(radius);
    live.setStreamingCenter(size / 2 + .5f, size / 2 + .5f);
    std::unique_ptr<OriginalWorld> context;
    std::vector<double> each;
    const auto start = Clock::now();
    for (;;) {
        auto requests = live.chunkRequests(1);
        if (requests.empty()) break;
        auto generated = OriginalWorld::generateChunk(requests[0], context);
        each.push_back(generated.loadMs);
        live.commitChunk(std::move(generated));
    }
    const double total = ms(start);
    std::sort(each.begin(), each.end());
    double sum = 0; for (double v : each) sum += v;
    std::printf("%s seed=%u size=%d chunks=%zu total=%.0fms mean=%.2fms p50=%.2fms p95=%.2fms max=%.2fms\n",
                carvers ? "v4" : "v3", seed, size, each.size(), total, sum / each.size(),
                each[each.size() / 2], each[each.size() * 95 / 100], each.back());
}
void lighting(uint32_t seed, int size, bool carvers) {
    OriginalWorld world(seed, size, true, true, carvers);
    world.setChunkRanges(3, 3); world.beginStreaming(3);
    world.setStreamingCenter(size / 2 + .5f, size / 2 + .5f);
    while (world.processChunkLoads(64)) {}
    const auto& volume = world.renderVolume(size / 2 + .5f, size / 2 + .5f, 80);
    worldrender::LightingBuffer buffer;
    const auto start = Clock::now();
    buffer.update(volume, carvers ? "v4" : "v3", 1, 0);
    const double full = ms(start);
    size_t darkAir = 0, litByBlock = 0;
    for (int y = 1; y < 60; ++y) for (int z = 0; z < volume.depth; ++z) for (int x = 0; x < volume.width; ++x) {
        const size_t i = (size_t(y) * volume.depth + z) * volume.width + x;
        if (volume.data[i]) continue;
        const auto s = buffer.sample(volume.originX + x, y, volume.originZ + z);
        darkAir += s.sky == 0 && s.block == 0;
        litByBlock += s.block > 0;
    }
    std::printf("%s lighting window=%dx%dx%d full-build=%.1fms underground-air(y<60): dark=%zu block-lit=%zu\n",
                carvers ? "v4" : "v3", volume.width, volume.height, volume.depth, full, darkAir, litByBlock);
}
}
int main(int argc, char** argv) {
    const uint32_t seed = argc > 1 ? uint32_t(std::strtoul(argv[1], nullptr, 10)) : 12345u;
    const int radius = argc > 2 ? std::atoi(argv[2]) : 12;
    const int size = argc > 3 ? std::atoi(argv[3]) : 5120;
    for (bool carvers : {false, true}) generation(seed, size, radius, carvers);
    for (bool carvers : {false, true}) lighting(seed, size, carvers);
}
