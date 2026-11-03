#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace original_structures {
// All callback inputs and Block/Landmark positions are ORIGINAL absolute block
// coordinates [-worldSize/2, worldSize/2). App mapping is x/z + worldSize/2.
struct Block { int x, y, z; uint8_t tile, data; };
struct Landmark {
    int64_t id; // stable packed source start-chunk coordinates, not a catalog ID
    std::string type;
    int sourceX, sourceZ, biome;
    int minX, minZ, maxX, maxZ;
    int pieceCount;
};
struct Options {
    // Optional ORIGINAL pre-Voronoi Layer sample, indexed in quarter-resolution
    // cells, needed for exact BiomeSource::containsOnly. If absent, biome(x*4,z*4)
    // is used and biome-boundary placement is NOT upstream-equivalent.
    std::function<int(int, int)> generationBiome;
    size_t cacheRegions = 128; // immutable whole-village emission cache, bounded LRU
};
class Generator {
public:
    // biome: original full-resolution biome ID; groundHeight: base terrain first
    // air Y above top motion-blocking non-leaf block (upstream getTopSolidBlock).
    // rawTile: BASE terrain original TileID, never structures or player edits.
    // Callbacks must be pure, valid across the entire world, and non-recursive.
    Generator(int64_t seed, int worldSize,
              std::function<int(int,int)> biome,
              std::function<int(int,int)> groundHeight,
              std::function<uint8_t(int,int,int)> rawTile,
              Options options = Options());
    ~Generator();
    Generator(Generator&&) noexcept;
    Generator& operator=(Generator&&) noexcept;
    Generator(const Generator&) = delete;
    Generator& operator=(const Generator&) = delete;
    // Final writes, including air carving; unique positions, stable x/y/z order.
    // Clip is exactly this source chunk, Y=1..255 and finite world bounds.
    std::vector<Block> generateForChunk(int sourceChunkX, int sourceChunkZ);
    // Square Chebyshev radius in SOURCE BLOCKS around village start positions.
    std::vector<Landmark> nearby(int sourceX, int sourceZ, int radiusBlocks);
    // Original IDs potentially emitted, and those absent from an app's catalog.
    static std::vector<uint8_t> emittedTileIDs();
    static std::vector<uint8_t> unsupportedIDs(const std::function<bool(uint8_t)>& supported);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace original_structures
