#include "Platform.h"
#include <algorithm>
#include <mutex>
#include <stdexcept>
#include <tuple>

namespace original_carvers { namespace detail {
App app;
void initialize() {
    // Original trig lookup initialized once, avoiding the lazy-init data race
    // in Mth::sin when several isolated worker contexts start concurrently.
    static std::once_flag once;
    std::call_once(once, [] { Mth::init(); });
}
namespace {
constexpr int RegionWidth = 32, RegionHeight = 256;
size_t regionIndex(int x, int y, int z) { return (size_t(y) * RegionWidth + z) * RegionWidth + x; }
}
void Level::bindChunk(int cx, int cz, const uint8_t* top) {
    if (!top) throw std::invalid_argument("carver requires per-block biome topMaterial");
    chunkX_ = cx; chunkZ_ = cz; top_ = top;
}
Biome* Level::getBiome(int x, int z) {
    // Carvers only query blocks of the chunk being carved (xx + xOffs*16).
    const int lx = x - chunkX_ * 16, lz = z - chunkZ_ * 16;
    if (!top_ || lx < 0 || lx > 15 || lz < 0 || lz > 15) throw std::logic_error("carver biome query outside carved chunk");
    biome_.topMaterial = top_[lx + lz * 16];
    return &biome_;
}
void Level::bindRegion(int xt, int zt, const ChunkTiles& tiles) {
    if (!tiles) throw std::invalid_argument("ore planning requires a pre-decoration tile source");
    x0_ = xt * 16; z0_ = zt * 16;
    region_.assign(size_t(RegionWidth) * RegionHeight * RegionWidth, 0);
    heights_.assign(size_t(RegionWidth) * RegionWidth, -1);
    writes_.clear();
    std::vector<uint8_t> cells(16 * 256 * 16);
    for (int dz = 0; dz < 2; ++dz) for (int dx = 0; dx < 2; ++dx) {
        std::fill(cells.begin(), cells.end(), uint8_t(0));
        if (!tiles(xt + dx, zt + dz, cells.data())) continue; // outside the finite world: air
        for (int y = 0; y < 256; ++y) for (int z = 0; z < 16; ++z)
            std::copy_n(&cells[(size_t(y) * 16 + z) * 16], 16, &region_[regionIndex(dx * 16, y, dz * 16 + z)]);
    }
}
bool Level::local(int& x, int y, int& z) const {
    x -= x0_; z -= z0_;
    return x >= 0 && x < RegionWidth && z >= 0 && z < RegionWidth && y >= 0 && y < RegionHeight;
}
int Level::getTile(int x, int y, int z) {
    if (!local(x, y, z)) return 0;
    return region_[regionIndex(x, y, z)];
}
bool Level::setTileAndData(int x, int y, int z, int tile, int data, int) {
    const int sx = x, sz = z;
    if (tile < 0 || tile > 255 || data < 0 || data > 15) throw std::out_of_range("original ore TileID/data");
    if (!local(x, y, z)) return false;
    region_[regionIndex(x, y, z)] = uint8_t(tile);
    writes_.push_back(Block{sx, y, sz, uint8_t(tile), uint8_t(data)});
    return true;
}
int Level::getHeightmap(int x, int z) {
    if (!local(x, 0, z)) return 0;
    auto& cached = heights_[size_t(z) * RegionWidth + x];
    if (cached < 0) {
        // First air above the topmost non-air pre-decoration tile. Ore writes
        // only replace stone with non-air, so this never changes mid-plan.
        int y = RegionHeight;
        while (y > 0 && !region_[regionIndex(x, y - 1, z)]) --y;
        cached = int16_t(y);
    }
    return cached;
}
std::vector<Block> Level::takeWrites() {
    std::vector<Block> result;
    result.swap(writes_);
    std::sort(result.begin(), result.end(), [](const Block& a, const Block& b) {
        return std::tie(a.x, a.y, a.z) < std::tie(b.x, b.y, b.z);
    });
    for (size_t i = 1; i < result.size(); ++i)
        if (result[i - 1].x == result[i].x && result[i - 1].y == result[i].y && result[i - 1].z == result[i].z)
            throw std::logic_error("ore cell written twice");
    return result;
}
BiomeDecorator::BiomeDecorator() {
#include "OreFeatures.inc"
}
BiomeDecorator::~BiomeDecorator() {
    for (Feature* f : {dirtOreFeature, gravelOreFeature, coalOreFeature, ironOreFeature,
                       goldOreFeature, redStoneOreFeature, diamondOreFeature, lapisOreFeature}) delete f;
}
namespace {
// RandomLevelSource members used by the exact getChunk carver statements.
struct CarverSource : ChunkSource {
    LargeCaveFeature* caveFeature = nullptr;
    CanyonFeature* canyonFeature = nullptr;
    Level* level = nullptr;
    void carve(int xOffs, int zOffs, byteArray blocks) {
#include "CarverOrder.inc"
    }
};
void seedOres(Level* level, Random* pprandom, int xt, int zt) {
#include "ChunkSeed.inc"
}
}
} // namespace detail

using namespace detail;
struct Carver::Impl {
    Level level;
    LargeCaveFeature cave;
    CanyonFeature canyon;
    CarverSource source;
    explicit Impl(int64_t seed) : level(seed) {
        source.caveFeature = &cave; source.canyonFeature = &canyon; source.level = &level;
    }
};
Carver::Carver(int64_t seed) { initialize(); impl_.reset(new Impl(seed)); }
Carver::~Carver() = default;
void Carver::apply(int cx, int cz, uint8_t* tiles, const uint8_t* top) {
    if (!tiles) throw std::invalid_argument("carver requires a 16x128x16 tile array");
    impl_->level.bindChunk(cx, cz, top);
    impl_->source.carve(cx, cz, byteArray(tiles, 16 * Level::genDepth * 16));
}
std::vector<Block> planOres(int64_t seed, int xt, int zt, const ChunkTiles& tiles) {
    initialize();
    Level level(seed);
    level.bindRegion(xt, zt, tiles);
    Random pprandom(seed);
    seedOres(&level, &pprandom, xt, zt);
    BiomeDecorator decorator;
    decorator.level = &level; decorator.random = &pprandom;
    decorator.xo = xt * 16; decorator.zo = zt * 16;
    decorator.decorateOres();
    return level.takeWrites();
}
}
