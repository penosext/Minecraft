#include "PlatformPch.h"
#include "original_biomes/Source.h"

#include <future>
#include <iostream>

namespace {
using original_biomes::BiomeData;
using original_biomes::Source;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool equal(const std::vector<BiomeData>& a, const std::vector<BiomeData>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto& x = a[i]; const auto& y = b[i];
        if (x.id != y.id || x.depth != y.depth || x.scale != y.scale ||
            x.temperature != y.temperature || x.top != y.top || x.material != y.material) return false;
    }
    return true;
}
std::uint64_t fingerprint(const std::vector<BiomeData>& a) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const auto& b : a) { hash ^= static_cast<std::uint64_t>(b.id); hash *= 1099511628211ULL; }
    return hash;
}
struct Original {
    original_layers::LayerArray layers;
    Original(std::int64_t seed, bool large) : layers(original_layers::Layer::getDefaultLayers(
        seed, large ? original_layers::LevelType::lvl_largeBiomes : original_layers::LevelType::lvl_normal)) {}
    ~Original() { delete[] layers.data; }
    std::vector<int> area(int index, int x, int z, int w, int h) {
        original_layers::IntCache::Reset();
        const auto a = layers[index]->getArea(x, z, w, h);
        std::vector<int> result(a.data, a.data + w * h);
        original_layers::IntCache::Reset();
        return result;
    }
};
void metadata() {
    const float depth[] = {-1,.1f,.1f,.3f,.1f,.1f,-.2f,-.5f,.1f,.1f,-1,-.5f,.1f,.3f,.2f,-1,0,.3f,.3f,.3f,.2f,.2f,1.8f};
    const float scale[] = {.4f,.3f,.2f,1.5f,.3f,.4f,.1f,0,.3f,.3f,.5f,0,.3f,1.3f,1,.1f,.1f,.8f,.7f,.8f,.8f,.4f,.5f};
    const float temp[] = {.5f,.8f,2,.2f,.7f,.05f,.8f,.5f,2,.5f,0,0,0,0,.9f,.9f,.8f,2,.7f,.05f,.2f,1.2f,1.2f};
    for (int id = 0; id < 23; ++id) {
        const auto& b = *original_layers::Biome::biomes[id];
        require(b.id == id && b.depth == depth[id] && b.scale == scale[id] && b.temperature == temp[id],
                "registered terrain metadata differs from original values");
        const int top = (id == 2 || id == 16 || id == 17) ? 12 :
                        (id == 14 || id == 15) ? 110 : id == 9 ? 3 : 2;
        const int material = (id == 2 || id == 16 || id == 17) ? 12 : 3;
        require(b.topMaterial == top && b.material == material, "original tile ID mismatch");
    }
}
void consistency(std::int64_t seed, bool large) {
    Source source(seed, large), repeated(seed, large);
    const auto raw = source.raw(-151, -99, 77, 53);
    const auto zoomed = source.zoomed(-151, -99, 77, 53);
    require(equal(raw, repeated.raw(-151, -99, 77, 53)), "same seed raw differs");
    require(equal(zoomed, repeated.zoomed(-151, -99, 77, 53)), "same seed Voronoi differs");
    (void)source.raw(211, -5, 257, 81);
    (void)source.zoomed(-2001, 99, 1, 37);
    require(equal(raw, source.raw(-151, -99, 77, 53)), "query order changed raw seed state");
    require(equal(zoomed, source.zoomed(-151, -99, 77, 53)), "query order changed Voronoi seed state");
    Original original(seed, large);
    for (int index = 0; index < 2; ++index) {
        const auto& wrapped = index ? zoomed : raw;
        const auto direct = original.area(index, -151, -99, 77, 53);
        require(wrapped.size() == direct.size(), "original pipeline rectangle size");
        for (std::size_t i = 0; i < direct.size(); ++i)
            require(direct[i] == wrapped[i].id, "facade differs from compiled original getDefaultLayers");
    }
    const auto debug = original.area(2, -151, -99, 77, 53);
    for (std::size_t i = 0; i < raw.size(); ++i) require(raw[i].id == debug[i], "raw/debug original pipeline differs");
    // Coherence across odd, negative rectangles and single-cell access.
    for (bool voronoi : {false, true}) {
        const auto all = voronoi ? source.zoomed(-43,-29,31,23) : source.raw(-43,-29,31,23);
        const auto sub = voronoi ? source.zoomed(-36,-24,13,9) : source.raw(-36,-24,13,9);
        for (int z = 0; z < 9; ++z)
            for (int x = 0; x < 13; ++x)
                require(all[x+7+(z+5)*31].id == sub[x+z*13].id, "negative rectangle partition seam");
        for (int z : {0,7,22}) for (int x : {0,11,30}) {
            const auto cell = voronoi ? source.zoomed(x-43,z-29,1,1) : source.raw(x-43,z-29,1,1);
            require(cell[0].id == all[x+z*31].id, "single cell and rectangle disagree");
        }
    }
    std::cout << "seed=" << seed << " large=" << large << " raw=" << fingerprint(raw)
              << " voronoi=" << fingerprint(zoomed) << '\n';
}
}
int main() {
    try {
        metadata();
        for (bool large : {false,true})
            for (std::int64_t seed : {std::int64_t{0},std::int64_t{1},std::int64_t{-1},
                                      std::numeric_limits<std::int64_t>::min(),std::int64_t{123456789012345LL}})
                consistency(seed, large);
        Source a(1), b(2), again(1), large(1,true);
        const auto first = a.raw(-256,-256,512,512);
        require(!equal(first,b.raw(-256,-256,512,512)), "different seeds yielded identical raw");
        require(!equal(first,large.raw(-256,-256,512,512)), "large-biome flag ignored");
        require(equal(first,again.raw(-256,-256,512,512)), "interleaved Sources contaminated seed");
        const auto zi = a.zoomed(-1001,731,257,193);
        require(!equal(zi,b.zoomed(-1001,731,257,193)), "different seeds yielded identical Voronoi");
        auto future = std::async(std::launch::async, [&a] { return a.zoomed(-1001,731,257,193); });
        require(equal(zi,a.zoomed(-1001,731,257,193)), "shared Source concurrent main result");
        require(equal(zi,future.get()), "shared Source concurrent worker result");
        auto separate = std::async(std::launch::async, [] {
            Source source(1); return source.raw(-256,-256,512,512);
        });
        require(equal(first,separate.get()), "thread-local IntCache/Source mismatch");
        require(a.raw(0,0,0,1).empty() && a.zoomed(0,0,1,0).empty(), "empty query");
        bool rejected = false;
        try { (void)a.raw(0,0,-1,1); } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "negative size accepted");
        std::cout << "PASS: exact original raw/Voronoi pipeline, seed order, metadata, negative coordinates, seams, TLS\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n'; return 1;
    }
}
