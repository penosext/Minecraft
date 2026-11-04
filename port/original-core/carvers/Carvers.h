#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace original_carvers {
// SOURCE coordinates throughout: chunk (cx,cz) covers blocks [cx*16, cx*16+16).
struct Block { int x, y, z; std::uint8_t tile, data; };

// Original RandomLevelSource::getChunk carver stage: caveFeature->apply then
// canyonFeature->apply on the BASE (prepareHeights + buildSurfaces) array.
// One instance owns the original LargeFeature objects and their Random; it is
// single-thread-at-a-time (each async context owns its own Carver).
class Carver {
public:
    explicit Carver(std::int64_t seed);
    ~Carver();
    Carver(const Carver&) = delete;
    Carver& operator=(const Carver&) = delete;
    // tiles: 16*128*16 upstream (x*16+z)*128+y layout, modified in place.
    // top: 256 zoomed per-block biome topMaterial bytes, x + z*16 order.
    void apply(int sourceChunkX, int sourceChunkZ, std::uint8_t* tiles, const std::uint8_t* top);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Fill a zeroed 16*256*16 buffer in (y*16+z)*16+x order with the immutable
// PRE-DECORATION tiles (carved base + structures) of a source chunk. Return
// false for chunks outside the finite world (they stay air).
using ChunkTiles = std::function<bool(int sourceChunkX, int sourceChunkZ, std::uint8_t* cells)>;

// Original postProcess reseed chain followed by BiomeDecorator::decorateOres()
// for origin chunk (xt,zt). Writes reach only chunks xt..xt+1, zt..zt+1.
// Returns ore/dirt/gravel cells (each written at most once), sorted x/y/z.
// NOT full postProcess: see generated/source-manifest.json limitations.
std::vector<Block> planOres(std::int64_t seed, int sourceChunkX, int sourceChunkZ, const ChunkTiles& tiles);
}
