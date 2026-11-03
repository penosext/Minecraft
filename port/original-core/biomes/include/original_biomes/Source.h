#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace original_biomes {

struct BiomeData {
    int id;
    float depth, scale, temperature;
    std::uint8_t top, material;
};

// Coordinates and dimensions are in the original layer's own units:
// raw = pre-Voronoi biome cells (one cell per four world blocks),
// zoomed = Voronoi output in world blocks. Layout is x + z * width.
// Queries have no output cache, reset original IntCache every time, and are
// serialized per Source. Distinct Sources can be queried concurrently.
class Source {
public:
    explicit Source(std::int64_t seed, bool large = false);
    ~Source();
    Source(Source&&) noexcept;
    Source& operator=(Source&&) noexcept;
    Source(const Source&) = delete;
    Source& operator=(const Source&) = delete;

    std::vector<BiomeData> raw(int x, int z, int w, int h);
    std::vector<BiomeData> zoomed(int x, int z, int w, int h);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace original_biomes
