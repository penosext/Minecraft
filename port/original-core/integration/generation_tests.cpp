#include <original_biomes/Source.h>
#include "Generator.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <set>
#include <vector>

namespace {
std::vector<original_terrain::BiomeData> convert(const std::vector<original_biomes::BiomeData>& input)
{
    std::vector<original_terrain::BiomeData> output;
    for (const auto& biome : input)
        output.push_back({biome.id, biome.depth, biome.scale, biome.temperature, biome.top, biome.material});
    return output;
}

std::unique_ptr<original_terrain::Generator> generator(std::int64_t seed)
{
    auto biomes = std::make_shared<original_biomes::Source>(seed);
    original_terrain::Config settings;
    settings.seed = seed;
    return std::unique_ptr<original_terrain::Generator>(new original_terrain::Generator(settings,
        [biomes](int x, int z, int w, int h) { return convert(biomes->raw(x, z, w, h)); },
        [biomes](int x, int z, int w, int h) { return convert(biomes->zoomed(x, z, w, h)); }));
}

std::uint64_t hash(const std::vector<std::uint8_t>& values)
{
    std::uint64_t result = 1469598103934665603ull;
    for (auto value : values) { result ^= value; result *= 1099511628211ull; }
    return result;
}
}

int main(int argc, char** argv)
{
    std::set<std::uint8_t> all;
    bool seedDifference = false;
    for (const auto seed : {std::int64_t(0), std::int64_t(12345), std::int64_t(-987654321), std::int64_t(0x123456789abcd)}) {
        auto first = generator(seed), repeat = generator(seed);
        const auto base = first->generateChunk(0, 0);
        assert(base.tiles.size() == 16 * 128 * 16);
        assert(base.biomes.size() == 256);
        assert(base.tiles == repeat->generate(0, 0));
        std::array<int, 256> counts{};
        for (auto value : base.tiles) { ++counts[value]; all.insert(value); }
        assert(counts[1] > 1000 && counts[7] > 0 && counts[0] > 1000);
        const auto origin = first->density(0, 0);
        const auto east = first->density(1, 0);
        const auto south = first->density(0, 1);
        assert(origin.size() == 5 * 5 * 17);
        for (int axis = 0; axis < 5; ++axis) for (int y = 0; y < 17; ++y) {
            assert(std::abs(origin[(4 * 5 + axis) * 17 + y] - east[axis * 17 + y]) < 1e-10);
            assert(std::abs(origin[(axis * 5 + 4) * 17 + y] - south[axis * 5 * 17 + y]) < 1e-10);
        }
        const auto negative = first->generate(-1, -1);
        assert(negative == repeat->generate(-1, -1));
        auto different = generator(seed + 1);
        seedDifference = seedDifference || base.tiles != different->generate(0, 0);
        std::cout << "seed=" << seed << " chunk(0,0) hash=" << std::hex << hash(base.tiles)
                  << std::dec << " stone=" << counts[1] << " bedrock=" << counts[7] << '\n';
        if (argc > 1 && seed == 12345) {
            std::ofstream output(argv[1], std::ios::binary);
            output.write(reinterpret_cast<const char*>(base.tiles.data()), base.tiles.size());
        }
    }
    assert(seedDifference);
    std::cout << "Original base-generation chain passed; observed tile IDs:";
    for (auto value : all) std::cout << ' ' << int(value);
    std::cout << "\nScope excludes caves, structures, decoration, original Level, physics and saves.\n";
}
