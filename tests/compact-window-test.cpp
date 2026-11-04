// Regression: preload radius > 3 enables compact chunks (upper air trimmed).
// The window rebuild used to copy a full 256-high column from the trimmed
// vector, reading heap memory past its end ("ghost terrain" after raising the
// range), and setBlocks wrote past the end when placing above the stored top.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "OriginalWorld.hpp"
#include <cassert>
#include <cstdio>

namespace {
using mcworld::OriginalWorld;
constexpr float Center = 440.5f;

void loadAll(OriginalWorld& world) {
    for (int guard = 0; world.streamingInfo().queued > 0; ++guard) {
        assert(guard < 1000);
        world.processChunkLoads(8);
    }
}

// Every window cell must equal the bounds-checked blockAt() of a resident column.
void windowMatchesStorage(const OriginalWorld& world) {
    const auto& v = world.renderVolume(Center, Center);
    assert(v.loadedColumns);
    size_t checked = 0;
    for (int z = 0; z < v.depth; ++z) for (int x = 0; x < v.width; ++x) {
        if (!v.loadedColumns[size_t(z) * v.width + x]) continue;
        for (int y = 0; y < v.height; ++y) {
            const uint16_t window = v.data[(size_t(y) * v.depth + z) * v.width + x];
            const uint16_t stored = world.blockAt(v.originX + x, y, v.originZ + z);
            if (window != stored) {
                std::fprintf(stderr, "mismatch at %d,%d,%d window=%u stored=%u\n",
                             v.originX + x, y, v.originZ + z, window, stored);
                assert(false);
            }
            ++checked;
        }
    }
    assert(checked > 0);
}

void compactWindowRebuild() {
    OriginalWorld world(12345, 864, false, false);
    world.beginStreaming(1); world.setStreamingCenter(Center, Center);
    world.setChunkRanges(2, 4); // preload 4 => compact()
    assert(world.streamingInfo().preloadRadius == 4);
    loadAll(world);
    windowMatchesStorage(world);
    // Raising display after load forces a full rebuild from trimmed chunks.
    world.setChunkRanges(4, 4);
    loadAll(world);
    windowMatchesStorage(world);
}

void compactEditAboveTrimmedTop() {
    OriginalWorld world(12345, 864, false, false);
    world.beginStreaming(1); world.setStreamingCenter(Center, Center);
    world.setChunkRanges(1, 4);
    loadAll(world);
    const int x = int(Center), z = int(Center);
    // y=250 is far above any generated terrain, i.e. past the trimmed end.
    assert(world.blockAt(x, 250, z) == 0);
    assert(world.setBlock(x, 250, z, 1));
    assert(world.blockAt(x, 250, z) == 1);
    windowMatchesStorage(world);
    assert(world.setBlock(x, 250, z, 0));
    assert(world.blockAt(x, 250, z) == 0);
    windowMatchesStorage(world);
}
} // namespace

int main() {
    compactWindowRebuild();
    compactEditAboveTrimmedTop();
    std::puts("compact-window: ok");
    return 0;
}
