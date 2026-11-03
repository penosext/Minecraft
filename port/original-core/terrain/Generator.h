#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace original_terrain {

// Field-for-field callback boundary; intentionally does not declare or guess
// original_biomes::BiomeData. Convert that module's result at the call site.
struct BiomeData {
    int id;
    float depth, scale, temperature;
    std::uint8_t top, material;
};

// Return exactly w*h elements in upstream layer order x + z*w. Coordinates
// passed to raw are already raw-layer coordinates: do NOT divide them again.
using BiomeQuery = std::function<std::vector<BiomeData>(int x, int z, int w, int h)>;

struct Config {
    std::int64_t seed = 0;
    int worldSize = 864; // blocks; adapter getXZSize() returns worldSize / 16
    int seaLevel = 63;
    bool classicEdgeMoat = false;
    bool smallEdgeMoat = false;
    bool mediumEdgeMoat = false;
};

struct Chunk {
    // Upstream prepareHeights layout: (x*16 + z)*128 + y. buildSurfaces'
    // original transposed reads/writes are deliberately not corrected.
    std::vector<std::uint8_t> tiles;
    std::vector<BiomeData> biomes; // zoomed callback order x + z*16
};

class Generator {
public:
    // No default biome or fake world. Both queries must be supplied. A real
    // original_biomes::Source is owned/captured by the caller, with the same seed.
    Generator(Config config, BiomeQuery raw, BiomeQuery zoomed);
    ~Generator();
    Generator(Generator&&) noexcept;
    Generator& operator=(Generator&&) noexcept;
    Generator(const Generator&) = delete;
    Generator& operator=(const Generator&) = delete;

    std::vector<std::uint8_t> generate(int chunkX, int chunkZ);
    Chunk generateChunk(int chunkX, int chunkZ);

    // Diagnostic outputs from the unchanged functions, before surfaces/features.
    std::vector<std::uint8_t> prepare(int chunkX, int chunkZ);
    // 5*5*17 doubles in (x*5 + z)*17 + y order, including shared chunk edges.
    std::vector<double> density(int chunkX, int chunkZ);

    static std::size_t index(int x, int y, int z);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace original_terrain
