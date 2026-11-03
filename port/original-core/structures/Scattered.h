#pragma once
#include "Structures.h"

namespace original_structures {
// Same pure BASE-terrain callbacks and SOURCE absolute coordinates as Generator.
// Original scattered placement: spacing=32, separation=8, salt=14357617;
// biome IDs 2/17 -> DesertPyramid, 21/22 -> JunglePyramid, 6 -> SwamplandHut.
class ScatteredGenerator {
public:
    ScatteredGenerator(int64_t seed, int worldSize,
                       std::function<int(int,int)> biome,
                       std::function<int(int,int)> groundHeight,
                       std::function<uint8_t(int,int,int)> rawTile);
    ~ScatteredGenerator();
    ScatteredGenerator(ScatteredGenerator&&) noexcept;
    ScatteredGenerator& operator=(ScatteredGenerator&&) noexcept;
    ScatteredGenerator(const ScatteredGenerator&) = delete;
    ScatteredGenerator& operator=(const ScatteredGenerator&) = delete;
    // Canonical whole-piece postProcess, then immutable chunk filtering. Includes
    // air writes; unique positions sorted x/y/z; world bounds and Y=1..255 only.
    // As with Village this intentionally differs from upstream per-chunk
    // decoration RNG/first-chunk height averaging. Desert remains at original Y=64.
    std::vector<Block> generateForChunk(int sourceChunkX, int sourceChunkZ);
    // Chebyshev radius in SOURCE BLOCKS around candidate chunk centers (+8,+8).
    // Bboxes retain original whole-piece XZ bounds, including clipped edge pieces.
    std::vector<Landmark> nearby(int sourceX, int sourceZ, int radiusBlocks);
    // Original TileIDs, not app catalog IDs. Chest/dispenser blocks and mechanism
    // metadata are retained, but inventories, redstone physics and entities are not.
    static std::vector<uint8_t> emittedTileIDs();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace original_structures
