#pragma once
#include "../structures/Structures.h"
#include <cstddef>

namespace original_decoration {
using Block = original_structures::Block;
struct Options {
    std::size_t cachePlans = 128;
    bool plants = true;
};
// SOURCE coordinates throughout: [-worldSize/2, worldSize/2), not app coordinates.
// This is an original-feature SUBSET schedule, NOT full postProcess equivalence.
// genDepth=128; maxBuildHeight=256. See generated/source-manifest.json.
class Generator {
public:
    // Callbacks must be pure PRE-DECORATION queries (base + optional immutable
    // structures), never decoration/player edits/final chunks or calls back into
    // this generator. ground returns firstAirY above the motion-blocking surface.
    // rawTile must include water/air and work at Y=0..255, returning 0 above terrain.
    Generator(int64_t seed, int worldSize,
              std::function<int(int,int)> biome,
              std::function<int(int,int)> ground,
              std::function<uint8_t(int,int,int)> rawTile,
              Options options = Options());
    ~Generator();
    Generator(Generator&&) noexcept;
    Generator& operator=(Generator&&) noexcept;
    Generator(const Generator&) = delete;
    Generator& operator=(const Generator&) = delete;
    // Immutable origin-chunk plans; own + eight neighbors, fixed merge order,
    // clipped to requested chunk/world, unique positions sorted x/y/z.
    std::vector<Block> generateForChunk(int sourceChunkX, int sourceChunkZ);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
