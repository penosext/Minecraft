#include "Adapter.h"

#include <limits>
#include <mutex>

namespace original_terrain_detail {

DefaultOptionsBoundary app;

void BiomeSource::query(BiomeArray& out, const original_terrain::BiomeQuery& fn,
                        int x, int z, int w, int h, bool zoomed)
{
    auto values = fn(x, z, w, h);
    if (values.size() != static_cast<std::size_t>(w * h))
        throw std::invalid_argument("Biome callback must return exactly w*h values in x+z*w order");
    for (const auto& b : values) {
        if (b.id < 0 || b.id > 255 || !std::isfinite(b.depth) || b.depth <= -2.0f ||
            !std::isfinite(b.scale) || b.scale < 0.0f || !std::isfinite(b.temperature))
            throw std::invalid_argument("Invalid biome callback fields");
    }
    std::vector<Biome> owned;
    owned.reserve(values.size());
    for (const auto& b : values)
        owned.push_back(Biome{b.id, b.depth, b.scale, b.temperature, b.top, b.material});
    storage_.push_back(std::move(owned));
    std::unique_ptr<Biome*[]> pointers(new Biome*[values.size()]);
    for (std::size_t i = 0; i < values.size(); ++i)
        pointers[i] = &storage_.back()[i];
    if (zoomed) lastZoomed_ = std::move(values);
    // Callers use the original explicit delete[]; no owning-array destructor.
    delete[] out.data;
    out = BiomeArray(pointers.release(), static_cast<unsigned>(w * h));
}

void BiomeSource::getRawBiomeBlock(BiomeArray& out, int x, int z, int w, int h)
{
    query(out, raw_, x, z, w, h, false);
}

void BiomeSource::getBiomeBlock(BiomeArray& out, int x, int z, int w, int h, bool)
{
    query(out, zoomed_, x, z, w, h, true);
}

RandomLevelSource::RandomLevelSource(Level* level, std::int64_t seed)
{
    // These are two exact original constructor source segments, not a claim
    // that the original feature-dependent constructor was ported in full.
    try {
#include "TerrainConstructor.inc"
    } catch (...) {
        delete forestNoise;
        delete floatingIslandNoise;
        delete floatingIslandScale;
        delete depthNoise;
        delete scaleNoise;
        delete perlinNoise3;
        delete perlinNoise1;
        delete lperlinNoise2;
        delete lperlinNoise1;
        delete pprandom;
        delete random;
        throw;
    }
}

RandomLevelSource::~RandomLevelSource()
{
    // Adapter ownership only. Not the original destructor (which also expects
    // feature objects, and does not free pprandom).
    delete forestNoise;
    delete floatingIslandNoise;
    delete floatingIslandScale;
    delete depthNoise;
    delete scaleNoise;
    delete perlinNoise3;
    delete perlinNoise1;
    delete lperlinNoise2;
    delete lperlinNoise1;
    delete pprandom;
    delete random;
    delete[] pows.data;
}

void RandomLevelSource::seedChunk(int xOffs, int zOffs)
{
#include "TerrainChunkSeed.inc"
}

double RandomLevelSource::noiseProbe(int n, double x, double y, double z)
{
    PerlinNoise* noises[] = {lperlinNoise1, lperlinNoise2, perlinNoise1,
                            perlinNoise3, scaleNoise, depthNoise, forestNoise};
    if (n < 0 || n >= 7) throw std::out_of_range("noise probe");
    return noises[n]->getValue(x, y, z);
}

} // namespace original_terrain_detail

namespace original_terrain {
namespace {

Config validate(Config config, const BiomeQuery& raw, const BiomeQuery& zoomed)
{
    if (!raw || !zoomed) throw std::invalid_argument("Both real biome queries are required; no default biome");
    if (config.worldSize < 16 || config.worldSize > 30000000 || config.worldSize % 16 != 0)
        throw std::invalid_argument("worldSize must be positive blocks divisible by 16, at most 30000000");
    if (config.seaLevel < 1 || config.seaLevel >= 128)
        throw std::invalid_argument("seaLevel must be in [1,127]");
    return config;
}

void validateChunk(int x, int z)
{
    // Bound the wrapper's coordinate arithmetic, without altering the original
    // function bodies. Supports negative chunks and edge diagnostics.
    constexpr int limit = 30000000 / 16;
    if (x < -limit || x > limit || z < -limit || z > limit)
        throw std::out_of_range("Chunk coordinate exceeds original Level maximum");
}

std::mutex& terrainMutex()
{
    // Unchanged prepareHeights has shared performance counters. Serialize calls
    // across generators rather than changing those original function bytes.
    static std::mutex mutex;
    return mutex;
}

} // namespace

struct Generator::Impl {
    Config config;
    original_terrain_detail::BiomeSource source;
    original_terrain_detail::Level level;
    original_terrain_detail::RandomLevelSource terrain;

    Impl(Config cfg, BiomeQuery raw, BiomeQuery zoomed)
        : config(validate(cfg, raw, zoomed)), source(std::move(raw), std::move(zoomed)),
          level(config, &source), terrain(&level, config.seed) {}
};

Generator::Generator(Config config, BiomeQuery raw, BiomeQuery zoomed)
    : impl_(new Impl(config, std::move(raw), std::move(zoomed))) {}
Generator::~Generator() = default;
Generator::Generator(Generator&&) noexcept = default;
Generator& Generator::operator=(Generator&&) noexcept = default;

std::size_t Generator::index(int x, int y, int z)
{
    if (x < 0 || x >= 16 || z < 0 || z >= 16 || y < 0 || y >= 128)
        throw std::out_of_range("Tile coordinate outside 16x128x16 chunk");
    return static_cast<std::size_t>((x * 16 + z) * 128 + y);
}

std::vector<std::uint8_t> Generator::generate(int chunkX, int chunkZ)
{
    return generateChunk(chunkX, chunkZ).tiles;
}

Chunk Generator::generateChunk(int chunkX, int chunkZ)
{
    validateChunk(chunkX, chunkZ);
    std::lock_guard<std::mutex> lock(terrainMutex());
    auto& p = *impl_;
    p.source.clear();
    Chunk result;
    result.tiles.resize(16 * 128 * 16, 0);
    original_terrain_detail::byteArray blocks(result.tiles.data(), static_cast<unsigned>(result.tiles.size()));
    p.terrain.seedChunk(chunkX, chunkZ);
    p.terrain.prepareHeights(chunkX, chunkZ, blocks);
    original_terrain_detail::BiomeArray biomes;
    p.source.getBiomeBlock(biomes, chunkX * 16, chunkZ * 16, 16, 16, true);
    std::unique_ptr<original_terrain_detail::Biome*[]> ownBiomes(biomes.data);
    p.terrain.buildSurfaces(chunkX, chunkZ, blocks, biomes);
    result.biomes = p.source.lastZoomed();
    p.source.clear();
    return result;
}

std::vector<std::uint8_t> Generator::prepare(int chunkX, int chunkZ)
{
    validateChunk(chunkX, chunkZ);
    std::lock_guard<std::mutex> lock(terrainMutex());
    auto& p = *impl_;
    p.source.clear();
    std::vector<std::uint8_t> result(16 * 128 * 16, 0);
    p.terrain.seedChunk(chunkX, chunkZ);
    p.terrain.prepareHeights(chunkX, chunkZ,
        original_terrain_detail::byteArray(result.data(), static_cast<unsigned>(result.size())));
    p.source.clear();
    return result;
}

std::vector<double> Generator::density(int chunkX, int chunkZ)
{
    validateChunk(chunkX, chunkZ);
    std::lock_guard<std::mutex> lock(terrainMutex());
    auto& p = *impl_;
    p.source.clear();
    original_terrain_detail::BiomeArray biomes;
    p.source.getRawBiomeBlock(biomes, chunkX * 4 - 2, chunkZ * 4 - 2, 10, 10);
    std::unique_ptr<original_terrain_detail::Biome*[]> ownBiomes(biomes.data);
    doubleArray values = p.terrain.getHeights(doubleArray(), chunkX * 4, 0,
                                             chunkZ * 4, 5, 17, 5, biomes);
    std::unique_ptr<double[]> ownValues(values.data);
    std::vector<double> result(values.data, values.data + values.length);
    p.source.clear();
    return result;
}

} // namespace original_terrain
