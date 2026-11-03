#include "Adapter.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <set>
#include <string>
#include <tuple>

using original_terrain::BiomeData;
using original_terrain::BiomeQuery;
using original_terrain::Config;
using original_terrain::Generator;
namespace detail = original_terrain_detail;

namespace {
void require(bool ok, const std::string& text)
{
    if (!ok) throw std::runtime_error(text);
}

// These are explicitly synthetic test fixtures, NOT real game biome sources.
BiomeQuery uniform(BiomeData biome = {1, 0.1f, 0.2f, 0.8f, 2, 3})
{
    return [biome](int, int, int w, int h) { return std::vector<BiomeData>(w*h, biome); };
}

bool exact(double a, double b) { return std::memcmp(&a, &b, sizeof(a)) == 0; }

struct Fixture {
    detail::BiomeSource source;
    detail::Level level;
    detail::RandomLevelSource terrain;
    explicit Fixture(Config config = Config(), BiomeQuery query = uniform())
        : source(query, query), level(config, &source), terrain(&level, config.seed) {}
};

void constructor()
{
    Config config;
    config.seed = -INT64_C(0x123456789abcdef);
    Fixture f(config);
    require(!f.terrain.powsInitialized(), "pows must initially be original empty shallow array");
    Random expected(config.seed);
    Random expectedPP(config.seed);
    (void)expectedPP;
    const int levels[] = {16, 16, 8, 4, 10, 16, 8};
    for (int n = 0; n < 7; ++n) {
        PerlinNoise oracle(&expected, levels[n]);
        require(exact(oracle.getValue(-1.25, 3.75, 9.125),
                      f.terrain.noiseProbe(n, -1.25, 3.75, 9.125)),
                "constructor noise order/state " + std::to_string(n));
    }
    require(exact(expected.nextDouble(), f.terrain.nextRandomForTest()), "constructor final random state");
    f.terrain.seedChunk(-7, 3);
    Random chunk(-7 * INT64_C(341873128712) + 3 * INT64_C(132897987541));
    require(exact(chunk.nextDouble(), f.terrain.nextRandomForTest()), "original chunk seed line");
    require(f.level.getLevelData()->getXZSize() == 54 && f.level.seaLevel == 63, "size/sea adapter units");
    require(detail::app.getLevelGenerationOptions() == nullptr, "default original options/no override");
    require(detail::Mth::sqrt(4.0f) == 2.0f && detail::Mth::sqrt(9.0) == 3.0f, "exact Mth extraction");
}

void determinism()
{
    Config config;
    config.seed = INT64_C(0x12345678abcdef01);
    Generator a(config, uniform(), uniform()), b(config, uniform(), uniform());
    const auto initial = a.generate(-7, 3);
    a.generate(2, -9);
    a.density(8, 7);
    require(initial == a.generate(-7, 3) && initial == b.generate(-7, 3), "seed/repeat/order determinism");
    ++config.seed;
    Generator c(config, uniform(), uniform());
    require(initial != c.generate(-7, 3), "different parent seed must alter actual noise terrain");
}

void randomstone()
{
    Generator a(Config(), uniform(), uniform());
    auto blocks = a.prepare(0, 0);
    std::size_t stones = 0, waters = 0, air = 0;
    for (auto block : blocks) {
        stones += block == detail::Tile::stone_Id;
        waters += block == detail::Tile::calmWater_Id;
        air += block == 0;
        require(block == 0 || block == 1 || block == 9, "prepareHeights only emits original air/stone/water IDs");
    }
    require(stones > 4096 && air > 4096 && stones+air+waters == blocks.size(), "real density must produce stone and air");
    auto d = a.density(0, 0);
    require(d.size() == 425, "5*5*17 density samples");
    require(std::all_of(d.begin(), d.end(), [](double v) { return std::isfinite(v); }), "finite density");
    require(std::any_of(d.begin(), d.end(), [](double v) { return v > 0; }) &&
            std::any_of(d.begin(), d.end(), [](double v) { return v < 0; }), "nonconstant signed noise");

    // Test the real surface random stream, including the original per-y calls.
    Fixture f;
    std::vector<byte> input(32768, 1);
    detail::BiomeArray biomes;
    f.source.getBiomeBlock(biomes, 0, 0, 16, 16, true);
    std::unique_ptr<detail::Biome*[]> own(biomes.data);
    f.terrain.seedChunk(0, 0);
    f.terrain.buildSurfaces(0, 0, detail::byteArray(input.data(), input.size()), biomes);
    Random oracle(INT64_C(0));
    for (int x = 0; x < 16; ++x) for (int z = 0; z < 16; ++z) {
        oracle.nextDouble();
        for (int y = 127; y >= 0; --y) oracle.nextInt(2);
    }
    require(exact(oracle.nextDouble(), f.terrain.nextRandomForTest()), "surface random call order/count");
}

void water()
{
    Generator g(Config(), uniform(), uniform());
    auto blocks = g.prepare(26, 0); // x=431 is exact positive world edge
    for (int z = 0; z < 16; ++z) for (int y = 0; y < 128; ++y) {
        const byte b = blocks[Generator::index(15, y, z)];
        if (y <= 53) require(b == 1, "edge rock through sea-10 inclusive");
        else if (y < 63) require(b == 9, "edge sea below63");
        else require(b == 0, "edge air above sea");
    }
    Fixture f;
    int emin = -1;
    require(f.terrain.getHeightFalloff(431, 0, &emin) == 128 && emin == 0, "positive edge falloff");
    require(f.terrain.getHeightFalloff(-432, 0, &emin) == 128 && emin == 0, "negative edge falloff");
    require(f.terrain.getHeightFalloff(399, 0, &emin) == 0 && emin == 32, "falloff boundary");
    require(f.terrain.getMinDistanceToEdge(432, 432, 864, 32) == 1, "original Euclidean corner truncation");
    Config expanded;
    expanded.worldSize = 1024;
    expanded.classicEdgeMoat = true;
    Fixture moat(expanded);
    require(moat.terrain.getHeightFalloff(431, 0, &emin) == 128 && emin == 0, "classic expansion moat");
}

void bedrock()
{
    Generator g(Config(), uniform(), uniform());
    const auto blocks = g.generate(-2, 4);
    int randomLayer = 0;
    for (int x = 0; x < 16; ++x) for (int z = 0; z < 16; ++z) {
        require(blocks[Generator::index(x, 0, z)] == 7 && blocks[Generator::index(x, 1, z)] == 7,
                "original two solid bedrock layers");
        randomLayer += blocks[Generator::index(x, 2, z)] == 7;
        for (int y = 3; y < 128; ++y) require(blocks[Generator::index(x, y, z)] != 7, "no bedrock above y2");
    }
    require(randomLayer > 0 && randomLayer < 256, "original random third bedrock layer");
}

void index()
{
    std::set<std::size_t> indices;
    for (int x = 0; x < 16; ++x) for (int z = 0; z < 16; ++z) for (int y = 0; y < 128; ++y)
        indices.insert(Generator::index(x,y,z));
    require(indices.size() == 32768 && *indices.begin() == 0 && *indices.rbegin() == 32767, "contiguous original layout");
    require(Generator::index(1, 0, 0) == 2048 && Generator::index(0, 0, 1) == 128, "original axis strides");
    bool threw = false;
    try { Generator::index(16,0,0); } catch (const std::out_of_range&) { threw = true; }
    require(threw, "public index validation");

    // Isolated deliberately asymmetric synthetic surface fixture. This locks
    // in, rather than repairs, the original buildSurfaces block transposition.
    BiomeQuery stripes = [](int, int, int w, int h) {
        std::vector<BiomeData> result(w*h);
        for (int z = 0; z < h; ++z) for (int x = 0; x < w; ++x)
            result[x+z*w] = {1, .1f, .2f, .8f, static_cast<byte>(x == 0 ? 2 : 12), 3};
        return result;
    };
    Fixture f(Config(), stripes);
    std::vector<byte> blocks(32768, 0);
    for (int x = 0; x < 16; ++x) for (int z = 0; z < 16; ++z)
        for (int y = 0; y <= 70; ++y) blocks[Generator::index(x,y,z)] = 1;
    detail::BiomeArray biomes;
    f.source.getBiomeBlock(biomes, 0,0,16,16,true);
    std::unique_ptr<detail::Biome*[]> own(biomes.data);
    f.terrain.seedChunk(0,0);
    f.terrain.buildSurfaces(0,0,detail::byteArray(blocks.data(), blocks.size()),biomes);
    for (int x = 0; x < 16; ++x) for (int z = 0; z < 16; ++z)
        require(blocks[Generator::index(x,70,z)] == (x == 0 ? 2 : 12), "preserve upstream biome/block transposition");
}

void seams()
{
    Config config;
    config.seed = -9919283;
    Generator g(config, uniform(), uniform());
    for (const auto& pos : {std::make_pair(0,0), std::make_pair(-1,-1), std::make_pair(26,0)}) {
        const auto a = g.density(pos.first, pos.second);
        const auto x = g.density(pos.first+1, pos.second);
        const auto z = g.density(pos.first, pos.second+1);
        for (int t = 0; t < 5; ++t) for (int y = 0; y < 17; ++y) {
            require(exact(a[(4*5+t)*17+y], x[t*17+y]), "shared x density nodes exact");
            require(exact(a[(t*5+4)*17+y], z[t*5*17+y]), "shared z density nodes exact");
        }
    }
}

void callbacks()
{
    std::vector<std::tuple<char,int,int,int,int>> calls;
    auto raw = [&](int x,int z,int w,int h) {
        calls.emplace_back('r',x,z,w,h);
        return uniform()(x,z,w,h);
    };
    auto zoom = [&](int x,int z,int w,int h) {
        calls.emplace_back('z',x,z,w,h);
        return uniform()(x,z,w,h);
    };
    Generator g(Config(), raw, zoom);
    const auto result = g.generateChunk(-7,3);
    require(calls.size() == 2 && calls[0] == std::make_tuple('r',-30,10,10,10) &&
            calls[1] == std::make_tuple('z',-112,48,16,16), "raw -> zoomed exact coordinates/order");
    require(result.tiles.size() == 32768 && result.biomes.size() == 256, "public chunk outputs");
    bool missing = false, badsize = false;
    try { Generator invalid(Config(), BiomeQuery(), uniform()); }
    catch (const std::invalid_argument&) { missing = true; }
    Generator invalid(Config(), [](int,int,int,int) { return std::vector<BiomeData>(); }, uniform());
    try { invalid.generate(0,0); } catch (const std::invalid_argument&) { badsize = true; }
    require(missing && badsize, "reject missing/faulty biome source, no fake fallback");
}
} // namespace

int main(int argc, char** argv)
{
    try {
        require(argc == 2, "expected one test case");
        const std::string name = argv[1];
        if (name == "constructor") constructor();
        else if (name == "determinism") determinism();
        else if (name == "randomstone") randomstone();
        else if (name == "water") water();
        else if (name == "bedrock") bedrock();
        else if (name == "index") index();
        else if (name == "seams") seams();
        else if (name == "callbacks") callbacks();
        else throw std::runtime_error("unknown test case");
        std::cout << "PASS " << name << " (synthetic test biomes only; no game-world claim)\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
