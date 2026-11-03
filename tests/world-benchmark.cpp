/* Native benchmark, without SDK/generated-file changes (run from repo root):
   g++ -std=c++17 -O3 -DNDEBUG -Wall -Wextra -Ijsapi/src/MinecraftRenderer \
       -Ijsapi/build/generated tests/world-benchmark.cpp \
       jsapi/src/MinecraftRenderer/{World,WorldRenderer,LegacyBlock,SpecialShapes}.cpp \
       -o /tmp/world-benchmark
   taskset -c 6 /tmp/world-benchmark [frames=10] [optional-output-prefix]
   Repeat with -O2 as well. Both implementations run in the same process,
   alternating timed batches with identical CPU affinity, seed and dimensions.
   Warmup/world creation/output/checksums/reference comparison are not timed.
*/
#define WORLD_RENDERER_REFERENCE_ONLY
#include "world-renderer-test.cpp"
#include <iomanip>
#include <stdexcept>
#include <string>

namespace {
struct TemporaryRoot {
    char path[40] = "/tmp/mc-world-benchmark.XXXXXX";
    TemporaryRoot() { if (!mkdtemp(path)) throw std::runtime_error("mkdtemp failed"); }
    ~TemporaryRoot() { std::error_code error; std::filesystem::remove_all(path, error); }
};
using Render = void (*)(uint32_t*, int, int, const mcworld::World&, float, float, int);
constexpr int width = 960, height = 266;
uint64_t checksum(const std::vector<uint32_t>& pixels)
{
    uint64_t hash = 14695981039346656037ull;
    for (uint32_t pixel : pixels) hash = (hash ^ pixel) * 1099511628211ull;
    return hash;
}
void writeImage(const std::string& path, const std::vector<uint32_t>& pixels)
{
    std::ofstream output(path, std::ios::binary);
    output << "P6\n" << width << ' ' << height << "\n255\n";
    for (uint32_t pixel : pixels) {
        const char rgb[] = {char(pixel >> 16), char(pixel >> 8), char(pixel)};
        output.write(rgb, 3);
    }
    if (!output) throw std::runtime_error("image write failed");
}
double measure(Render render, std::vector<uint32_t>& pixels, const mcworld::World& world, int frames, int step)
{
    const auto start = std::chrono::steady_clock::now();
    for (int frame = 0; frame < frames; ++frame)
        render(pixels.data(), width, height, world, 0, -10, step);
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}
}
int main(int argc, char** argv)
{
    const int frames = argc > 1 ? std::stoi(argv[1]) : 10;
    if (frames < 1) throw std::runtime_error("frames must be positive");
    TemporaryRoot root;
    mcworld::World world;
    world.create(12345, root.path, false);
    std::vector<uint32_t> reference(size_t(width) * height), optimized(reference.size());
    std::cout << "seed=12345 frames=" << frames << " size=" << width << 'x' << height
              << " yaw=0 pitch=-10 compiler=" << __VERSION__ << '\n';
    for (int warmup = 0; warmup < 3; ++warmup) {
        renderer_reference::render(reference.data(), width, height, world, 0, -10, 2);
        renderWorldFrame(optimized.data(), width, height, world, 0, -10, 2);
    }
    double oldTotal = 0, newTotal = 0;
    // AB/BA batches reduce CPU-frequency/order bias; each renderer still renders
    // exactly 'frames' timed frames, with no picture or voxel-cache shortcut.
    for (int frame = 0; frame < frames;) {
        const int count = std::min(2, frames - frame);
        if ((frame / 2) % 2 == 0) {
            oldTotal += measure(renderer_reference::render, reference, world, count, 2);
            newTotal += measure(renderWorldFrame, optimized, world, count, 2);
        } else {
            newTotal += measure(renderWorldFrame, optimized, world, count, 2);
            oldTotal += measure(renderer_reference::render, reference, world, count, 2);
        }
        frame += count;
    }
    const auto delta = renderer_reference::difference(reference, optimized);
    for (bool old : {true, false}) {
        const double ms = (old ? oldTotal : newTotal) / frames;
        std::cout << std::fixed << std::setprecision(3)
                  << (old ? "reference" : "optimized") << " step=2 rays=63840 ms/frame=" << ms
                  << " fps=" << 1000.0 / ms << " checksum=" << checksum(old ? reference : optimized) << '\n';
    }
    std::cout << "same-quality speedup=" << oldTotal / newTotal << "x differing_pixels=" << delta.pixels
              << " max_channel_error=" << delta.maxChannel << '\n';
    if (delta.maxChannel > 1) throw std::runtime_error("reference image mismatch");
    if (argc > 2) {
        writeImage(std::string(argv[2]) + "-reference-step2.ppm", reference);
        writeImage(std::string(argv[2]) + "-optimized-step2.ppm", optimized);
    }
    for (int warmup = 0; warmup < 3; ++warmup)
        renderWorldFrame(optimized.data(), width, height, world, 0, -10, 3);
    const double ms = measure(renderWorldFrame, optimized, world, frames, 3) / frames;
    std::cout << "optimized step=3 rays=28480 ms/frame=" << ms << " fps=" << 1000.0 / ms
              << " checksum=" << checksum(optimized) << " (default; NOT used for speedup)\n";
    if (argc > 2) writeImage(std::string(argv[2]) + "-optimized-step3.ppm", optimized);
}
