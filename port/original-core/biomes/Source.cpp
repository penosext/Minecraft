#include "PlatformPch.h"
#include "original_biomes/Source.h"

namespace original_biomes {
namespace {
// The original API owns a shallow LayerArray. Explicitly release that array,
// while shared_ptr's original concrete deleters release the Layer graph.
struct LayersDeleter {
    void operator()(original_layers::shared_ptr<original_layers::Layer>* p) const { delete[] p; }
};

struct ThreadCache {
    ThreadCache() { original_layers::IntCache::CreateNewThreadStorage(); }
    ~ThreadCache() { original_layers::IntCache::ReleaseThreadStorage(); }
};
void ensure_cache() {
    thread_local ThreadCache cache;
    (void)cache;
}
struct ResetOnExit {
    ~ResetOnExit() { original_layers::IntCache::Reset(); }
};

void validate(int x, int z, int w, int h) {
    if (w < 0 || h < 0) throw std::invalid_argument("negative biome dimensions");
    // Reject rectangles whose original int index/padding math could overflow.
    // This is input validation, not coordinate clamping or a replacement layer.
    constexpr std::int64_t max = std::numeric_limits<int>::max();
    for (int start : {x, z})
        if (static_cast<std::int64_t>(start) - 128 < std::numeric_limits<int>::min())
            throw std::out_of_range("biome coordinate underflows original layer padding");
    if (static_cast<std::int64_t>(x) + w + 128 > max ||
        static_cast<std::int64_t>(z) + h + 128 > max)
        throw std::out_of_range("biome rectangle overflows original coordinates");
    if ((static_cast<std::int64_t>(w) + 32) * (static_cast<std::int64_t>(h) + 32) > max / 4)
        throw std::length_error("biome rectangle overflows original layer allocation");
}
} // namespace

struct Source::Impl {
    // Original getDefaultLayers initializes raw first, then Voronoi, including
    // RiverMixerLayer's exact recursive biome/river seed initialization order.
    const std::int64_t seed;
    const bool large;
    std::unique_ptr<original_layers::shared_ptr<original_layers::Layer>[], LayersDeleter> layers;
    std::mutex mutex;
    Impl(std::int64_t seed_, bool large_) : seed(seed_), large(large_) {
        const auto a = original_layers::Layer::getDefaultLayers(
            seed, large ? original_layers::LevelType::lvl_largeBiomes : original_layers::LevelType::lvl_normal);
        layers.reset(a.data);
    }

    std::vector<BiomeData> query(int index, int x, int z, int w, int h) {
        validate(x, z, w, h);
        std::lock_guard<std::mutex> lock(mutex);
        ensure_cache();
        original_layers::IntCache::Reset();
        ResetOnExit cleanup;
        if (!w || !h) return {};
        const auto area = layers[index]->getArea(x, z, w, h);
        std::vector<BiomeData> result;
        result.reserve(static_cast<std::size_t>(w) * h);
        for (std::size_t i = 0, n = static_cast<std::size_t>(w) * h; i < n; ++i) {
            const int id = area[static_cast<unsigned int>(i)];
            if (id < 0 || id >= 256 || !original_layers::Biome::biomes[id])
                throw std::runtime_error("original layer returned an unregistered biome");
            const auto& b = *original_layers::Biome::biomes[id];
            result.push_back({id, b.depth, b.scale, b.temperature, b.topMaterial, b.material});
        }
        return result;
    }
};

Source::Source(std::int64_t seed, bool large) {
    // LevelType::staticCtor is original, executed once with safe static lifetime.
    static const bool initialized = [] {
        original_layers::LevelType::staticCtor();
        return true;
    }();
    (void)initialized;
    impl_ = std::make_unique<Impl>(seed, large);
}
Source::~Source() = default;
Source::Source(Source&&) noexcept = default;
Source& Source::operator=(Source&&) noexcept = default;
std::vector<BiomeData> Source::raw(int x, int z, int w, int h) {
    if (!impl_) throw std::logic_error("moved-from biome Source");
    return impl_->query(0, x, z, w, h);
}
std::vector<BiomeData> Source::zoomed(int x, int z, int w, int h) {
    if (!impl_) throw std::logic_error("moved-from biome Source");
    return impl_->query(1, x, z, w, h);
}
} // namespace original_biomes
