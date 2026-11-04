#include "OriginalWorld.hpp"

#include <cstdio>
#include "LegacyCatalog.hpp"
#include "Generator.h"
#include "original_biomes/Source.h"
#include "Structures.h"
#include "Decoration.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <chrono>
#include <set>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace mcworld {
namespace {
[[noreturn]] void invalid(const char* reason) { throw std::runtime_error(std::string("OriginalWorld: ") + reason); }
uint64_t nextEpoch() {
    // Shared by every live instance and every retarget, including recreated worlds
    // at the same address/configuration. Relaxed is sufficient for uniqueness.
    static std::atomic<uint64_t> next {1};
    const uint64_t value = next.fetch_add(1, std::memory_order_relaxed);
    if (!value) std::terminate(); // Never allow wraparound to resurrect old work.
    return value;
}
uint16_t keyId(const std::string& key) {
    for (int i = 0; i < legacy::kBlockCount; ++i)
        if (key == legacy::kBlockSpecs[i].key) return uint16_t(i + 1);
    throw std::runtime_error("OriginalWorld: required original key missing from catalog: " + key);
}
uint16_t oriented(const char* key, uint8_t data) {
    const std::string exact = std::string(key) + "_data_" + std::to_string(data);
    for (int i = 0; i < legacy::kBlockCount; ++i)
        if (exact == legacy::kBlockSpecs[i].key) return uint16_t(i + 1);
    // Existing catalog has one representative orientation per material. This
    // limitation is advertised in World status, never disguised as another tile.
    return keyId(key);
}
std::vector<original_terrain::BiomeData> convert(const std::vector<original_biomes::BiomeData>& input) {
    std::vector<original_terrain::BiomeData> result;
    result.reserve(input.size());
    for (const auto& b : input) {
        if (b.id < 0 || b.id >= 24) invalid("unsupported original biome id");
        result.push_back({b.id, b.depth, b.scale, b.temperature, b.top, b.material});
    }
    return result;
}
size_t cell(int x, int y, int z) { return (size_t(y) * 16 + z) * 16 + x; }
bool inside(int x, int y, int z, int size) {
    return x >= 0 && x < size && y >= 0 && y < OriginalWorld::Height && z >= 0 && z < size;
}
} // namespace

const OriginalWorld::BiomeMeta& OriginalWorld::biomeMeta(int id) {
    // Names: pinned Biome.cpp::staticCtor/setName. RGB: Biome::getGrassColor /
    // getFolageColor -> default texture pack port/assets/colours.xml, NOT the
    // unrelated Biome::setColor map tint or an invented climate approximation.
    static const BiomeMeta biomes[] = {
        {0, "海洋", "Ocean", 0x8eb971, 0x71a74d},
        {1, "平原", "Plains", 0x91bd59, 0x77ab2f},
        {2, "沙漠", "Desert", 0xbfb755, 0xaea42a},
        {3, "峭壁", "Extreme Hills", 0x8ab689, 0x6da36b},
        {4, "森林", "Forest", 0x79c05a, 0x59ae30},
        {5, "针叶林", "Taiga", 0x82b593, 0x63a277},
        {6, "沼泽", "Swampland", 0x5c694e, 0x496137},
        {7, "河流", "River", 0x8eb971, 0x71a74d},
        {8, "下界", "Hell", 0xbfb755, 0xaea42a},
        {9, "末地", "Sky", 0x8eb971, 0x71a74d},
        {10, "冻洋", "FrozenOcean", 0x80b497, 0x60a17b},
        {11, "冻河", "FrozenRiver", 0x80b497, 0x60a17b},
        {12, "冰原", "Ice Plains", 0x80b497, 0x60a17b},
        {13, "雪山", "Ice Mountains", 0x80b497, 0x60a17b},
        {14, "蘑菇岛", "MushroomIsland", 0x55c93f, 0x2bbb0f},
        {15, "蘑菇岛岸", "MushroomIslandShore", 0x55c93f, 0x2bbb0f},
        {16, "沙滩", "Beach", 0x91bd59, 0x77ab2f},
        {17, "沙漠丘陵", "DesertHills", 0xbfb755, 0xaea42a},
        {18, "森林丘陵", "ForestHills", 0x79c05a, 0x59ae30},
        {19, "针叶林丘陵", "TaigaHills", 0x82b593, 0x63a277},
        {20, "峭壁边缘", "Extreme Hills Edge", 0x8ab689, 0x6da36b},
        {21, "丛林", "Jungle", 0x53ca37, 0x29bc05},
        {22, "丛林丘陵", "JungleHills", 0x53ca37, 0x29bc05}
    };
    if (id < 0 || id >= int(sizeof(biomes) / sizeof(biomes[0]))) invalid("unregistered biome id");
    return biomes[id];
}

uint16_t OriginalWorld::catalogForTile(uint8_t tile, uint8_t data) {
    // Exact upstream Tile IDs + metadata, NEVER catalog-position assumptions.
    if (data > 15) invalid("invalid original tile metadata");
    static const char* woods[] = {"oak", "spruce", "birch", "jungle"};
    switch (tile) {
    case 0: return 0;
    case 1: return keyId("stone");
    case 2: return keyId("grass");
    case 3: return keyId("dirt");
    case 4: return keyId("cobblestone");
    case 5:
        if (data > 3) invalid("unsupported wood metadata");
        return keyId(std::string("planks_") + woods[data]);
    case 7: return keyId("unbreakable");
    case 8: return keyId("water");
    case 9: return keyId("calmWater");
    case 10: return keyId("lava");
    case 11: return keyId("calmLava");
    case 12: return keyId("sand");
    case 13: return keyId("gravel");
    case 14: return keyId("goldOre");
    case 15: return keyId("ironOre");
    case 16: return keyId("coalOre");
    case 17:
        if ((data & 12) == 12) invalid("all-bark log absent from catalog");
        return keyId(std::string("log_") + woods[data & 3] + ((data & 12) == 4 ? "_x" : (data & 12) == 8 ? "_z" : ""));
    case 18: return keyId(std::string("leaves_") + woods[data & 3]); // decay flags are not a material variant
    case 20: return keyId("glass");
    case 21: return keyId("lapisOre");
    case 23: return oriented("dispenser", data);
    case 24:
        if (data > 2) invalid("unsupported sandstone metadata");
        return keyId("sandstone_" + std::to_string(data));
    case 29: return oriented("pistonStickyBase", data);
    case 30: return keyId("web");
    case 31:
        if (data > 2) invalid("unsupported tallgrass metadata");
        return keyId("tallgrass_" + std::to_string(data));
    case 32: return keyId("deadBush");
    case 35: {
        static const char* colors[] = {"white", "orange", "magenta", "light_blue", "yellow", "lime", "pink", "gray",
                                       "silver", "cyan", "purple", "blue", "brown", "green", "red", "black"};
        if (data > 15) invalid("invalid wool color");
        return keyId(std::string("wool_") + colors[data]);
    }
    case 37: return keyId("flower");
    case 38: return keyId("rose");
    case 39: return keyId("mushroom_brown");
    case 40: return keyId("mushroom_red");
    case 43: return keyId("double_slab_" + std::to_string(data & 7) + (data == 8 ? "_seamless" : ""));
    case 44: return keyId("slab_" + std::to_string(data & 7) + (data & 8 ? "_upper" : ""));
    case 46: return keyId("tnt");
    case 47: return keyId("bookshelf");
    case 48: return keyId("mossyCobblestone");
    case 49: return keyId("obsidian");
    case 50: return oriented("torch", data);
    case 51: return keyId("fire");
    case 52: return keyId("mobSpawner");
    case 53: return oriented("stairs_wood", data);
    case 54: return oriented("chest", data);
    case 55: return oriented("redStoneDust", data);
    case 56: return keyId("diamondOre");
    case 58: return keyId("workBench");
    case 59:
        if (data > 7) invalid("unsupported wheat metadata");
        return keyId("wheat_" + std::to_string(data));
    case 60:
        if (data > 7) invalid("unsupported farmland metadata");
        return keyId(data ? "farmland_7" : "farmland_0"); // all moist levels share upstream texture
    case 61: return oriented("furnace", data);
    case 64: return oriented(data & 8 ? "door_wood_upper" : "door_wood_lower", data);
    case 65: return oriented("ladder", data);
    case 66: return oriented("rail", data);
    case 67: return oriented("stairs_stone", data);
    case 69: return oriented("lever", data);
    case 70: return keyId("pressurePlate_stone");
    case 71: return oriented(data & 8 ? "door_iron_upper" : "door_iron_lower", data);
    case 72: return keyId("pressurePlate_wood");
    case 73: return keyId("redStoneOre");
    case 77: return oriented("button", data);
    case 78:
        if (data > 7) invalid("unsupported snow-layer metadata");
        return keyId(data ? "snow_layer_" + std::to_string(data + 1) : "topSnow");
    case 79: return keyId("ice");
    case 80: return keyId("snow");
    case 81: return keyId("cactus");
    case 82: return keyId("clay");
    case 83: return keyId("reeds");
    case 85: return keyId("fence");
    case 93: return oriented("diode_off", data);
    case 98:
        if (data > 3) invalid("unsupported stonebrick metadata");
        return keyId("stonebrick_" + std::to_string(data));
    case 101: return keyId("ironFence");
    case 102: return keyId("thinGlass");
    case 106: return oriented("vine", data);
    case 109: return oriented("stairs_stoneBrickSmooth", data);
    case 110: return keyId("mycel");
    case 111: return keyId("waterLily");
    case 112: return keyId("netherBrick");
    case 113: return keyId("netherFence");
    case 114: return oriented("stairs_netherBricks", data);
    case 118: return oriented("cauldron", data);
    case 127:
        if ((data >> 2) > 2) invalid("unsupported cocoa age metadata");
        return keyId("cocoa_" + std::to_string(data >> 2)); // low two bits are facing
    case 128: return oriented("stairs_sandstone", data);
    case 131: return oriented("tripWireSource", data);
    case 132: return oriented("tripWire", data);
    case 134: return oriented("woodStairsDark", data);
    case 140: return oriented("flowerPot", data); // representative pot if content variant is absent
    case 141: case 142:
        if (data > 7) invalid("unsupported crop metadata");
        return keyId(std::string(tile == 141 ? "carrots_" : "potatoes_") + std::to_string(data == 7 ? 3 : std::min(2, int(data / 2))));
    default: throw std::runtime_error("OriginalWorld: unsupported original TileID " + std::to_string(tile) + " data " + std::to_string(data));
    }
}

struct OriginalWorld::Impl {
    struct Chunk {
        std::vector<uint16_t> baseline, blocks; // immutable generated source + player overlay
        std::array<uint8_t, 256> biomes {};
        uint64_t used = 0;
    };
    struct BaseChunk {
        std::vector<uint8_t> tiles; // upstream x-z-y order, no structures/edits
        std::array<uint8_t, 256> biomes {}, ground {};
        uint64_t used = 0;
    };
    struct StructureChunk {
        std::vector<uint8_t> tiles, data; // BASE+structures, y-z-x, all 256 levels
        uint64_t used = 0;
    };
    // Dependency LRUs remain small even with 625 FINAL targets. One extra FINAL
    // safety slot allows explicit spawn/physics loads without evicting targets.
    static constexpr size_t BaseCacheLimit = 7, StructureCacheLimit = 7;
    int size, chunks;
    uint32_t seed;
    uint64_t epoch = nextEpoch();
    bool isolated = false;
    original_biomes::Source source;
    original_terrain::Generator generator;
    std::unique_ptr<original_structures::Generator> structures;
    std::unique_ptr<original_decoration::Generator> decoration;
    mutable std::map<int, BaseChunk> baseCache;
    mutable std::map<int, StructureChunk> structureCache;
    mutable std::map<int, Chunk> cache;
    std::map<int, ModifiedChunk> modified;
    mutable uint64_t clock = 0;
    mutable std::vector<uint16_t> window;
    mutable std::vector<uint8_t> loadedColumns;
    mutable RenderVolume volume {nullptr, 0, 0, 0, 96, 256, 96};
    mutable int playerChunkX = -1, playerChunkZ = -1, nonair = 0;
    mutable int windowRadius = -1; // Visible rings currently published; grows with residency.
    bool streaming = false;
    int radius = 0, preloadRadius = 0, centerX = -1, centerZ = -1;
    std::set<int> target, displayTarget;
    std::vector<int> queue;
    mutable uint64_t generated = 0, readyRevision = 0;
    mutable bool windowDirty = true;
    double lastLoadMs = 0;
    mutable int deferredCount = 0;
    float yaw = 0, pitch = 0, aspect = 1;
    float eyeX = 0, eyeZ = 0;
    bool hasView = false;

    // The public view contract has no eye height. Test the chunk's full y=0..256
    // AABB against every possible in-world eye height, so vertical culling is
    // conservative without reading (or generating) unknown FINAL terrain.
    bool inView(int key) const {
        if (!hasView) return false;
        constexpr double rad = 0.017453292519943295;
        const double sy = std::sin(yaw * rad), cy = std::cos(yaw * rad);
        const double sp = std::sin(pitch * rad), cp = std::cos(pitch * rad);
        const std::array<double, 3> f {{sy * cp, sp, cy * cp}};
        const std::array<double, 3> r {{cy, 0, -sy}}, u {{-sy * sp, cp, -cy * sp}};
        const double tv = std::tan(35 * rad), th = tv * double(aspect);
        const double dx = (key % chunks) * 16.0 + 8 - eyeX;
        const double dz = (key / chunks) * 16.0 + 8 - eyeZ;
        const auto outside = [&](double x, double y, double z) {
            return x * dx + z * dz + std::abs(x) * 8 + std::abs(z) * 8 + std::abs(y) * Height < -1e-6;
        };
        if (outside(f[0], f[1], f[2])) return false; // behind the camera
        for (double sign : {-1.0, 1.0}) {
            if (outside(th * f[0] + sign * r[0], th * f[1] + sign * r[1], th * f[2] + sign * r[2])) return false;
            if (outside(tv * f[0] + sign * u[0], tv * f[1] + sign * u[1], tv * f[2] + sign * u[2])) return false;
        }
        return true;
    }
    void orderQueue(std::vector<int>& pending, int cx, int cz, const std::set<int>& display) const {
        const auto tier = [&](int key) {
            if (key == cz * chunks + cx) return 0; // center safety first
            if (display.count(key)) return inView(key) ? 1 : 2;
            return inView(key) ? 3 : 4;
        };
        std::sort(pending.begin(), pending.end(), [&](int a, int b) {
            const int ta = tier(a), tb = tier(b);
            if (ta != tb) return ta < tb;
            const int ax = a % chunks - cx, az = a / chunks - cz;
            const int bx = b % chunks - cx, bz = b / chunks - cz;
            const int da = ax * ax + az * az, db = bx * bx + bz * bz;
            return da != db ? da < db : a < b;
        });
    }
    void orderQueue() { orderQueue(queue, centerX, centerZ, displayTarget); }

    int windowSize() const { return radius ? (radius * 2 + 1) * 16 : 96; }
    // Raising the display radius must not expose a wall of unloaded columns: the
    // window only grows to the largest ring whose chunks are actually resident,
    // so the visible area fills in from the player outwards instead of showing a
    // sharp edge where terrain has not arrived yet. Returns a radius in chunks.
    int residentRadius(int cx, int cz) const {
        if (!streaming || !radius) return radius;
        // Never publish less than one ring, and never shrink a window that has
        // already been published: callers depend on a stable minimum geometry,
        // and shrinking would throw away terrain the player can already see.
        const int smallest = std::max(1, windowRadius);
        if (radius <= smallest) return radius;
        for (int ring = smallest; ring <= radius; ++ring) {
            const int x0 = std::max(0, cx - ring), x1 = std::min(chunks - 1, cx + ring);
            const int z0 = std::max(0, cz - ring), z1 = std::min(chunks - 1, cz + ring);
            for (int z = z0; z <= z1; ++z) for (int x = x0; x <= x1; ++x) {
                if (z != z0 && z != z1 && x != x0 && x != x1) continue; // ring border only
                if (!cache.count(z * chunks + x)) return ring == smallest ? smallest : ring - 1;
            }
        }
        return radius;
    }
    size_t cacheLimit() const {
        const size_t side = size_t(preloadRadius * 2 + 1);
        return std::max(CacheLimit, side * side + 1 + BaseCacheLimit + StructureCacheLimit);
    }
    bool compact() const { return preloadRadius > 3; }
    static uint16_t readCell(const std::vector<uint16_t>& cells, size_t at) {
        return at < cells.size() ? cells[at] : 0;
    }
    static size_t nonairSize(const std::vector<uint16_t>& cells) {
        const auto last = std::find_if(cells.rbegin(), cells.rend(), [](uint16_t v) { return v != 0; });
        return size_t(last.base() - cells.begin());
    }
    static void trimCells(std::vector<uint16_t>& cells) {
        const size_t count = nonairSize(cells);
        // resize alone retains the full allocation: exact replacement is needed
        // to release upper air on a memory-constrained device.
        if (cells.capacity() > count) std::vector<uint16_t>(cells.begin(), cells.begin() + std::ptrdiff_t(count)).swap(cells);
    }
    static void trimChunk(Chunk& chunk) {
        trimCells(chunk.baseline); trimCells(chunk.blocks);
    }
    int chunkHeight(int key, const Chunk& chunk) const {
        size_t count = nonairSize(chunk.blocks);
        const auto edits = modified.find(key);
        if (edits != modified.end() && !edits->second.edits.empty())
            count = std::max(count, size_t(edits->second.edits.rbegin()->first) + 1);
        return int((count + 255) / 256);
    }
    void invalidate() { windowDirty = true; ++readyRevision; }
    std::set<int> rangeTarget(int cx, int cz, int range) const {
        const int width = range * 2 + 1;
        const int ox = std::clamp(cx - range, 0, chunks - width);
        const int oz = std::clamp(cz - range, 0, chunks - width);
        std::set<int> result;
        for (int z = oz; z < oz + width; ++z)
            for (int x = ox; x < ox + width; ++x) result.insert(z * chunks + x);
        return result;
    }
    void retarget(int cx, int cz, int nextRadius, int nextPreloadRadius, bool force) {
        if (!streaming || (!force && centerX == cx && centerZ == cz &&
                          radius == nextRadius && preloadRadius == nextPreloadRadius)) return;
        // Prepare all allocating state first: a failed retarget cannot publish
        // mismatched radii/targets, retire residents or leave a partial queue.
        auto next = rangeTarget(cx, cz, nextPreloadRadius);
        auto nextDisplay = rangeTarget(cx, cz, nextRadius);
        std::vector<int> pending;
        pending.reserve(next.size());
        for (int key : next) if (!cache.count(key)) pending.push_back(key);
        orderQueue(pending, cx, cz, nextDisplay);
        const bool changed = centerX != cx || centerZ != cz || target != next ||
                             radius != nextRadius || preloadRadius != nextPreloadRadius;
        const uint64_t nextGeneration = changed ? nextEpoch() : epoch;
        // Publish both ranges once. Even display-only changes obsolete old work;
        // rebuilding saved overlays alone deliberately preserves its epoch.
        radius = nextRadius; preloadRadius = nextPreloadRadius;
        centerX = cx; centerZ = cz; epoch = nextGeneration;
        target.swap(next); displayTarget.swap(nextDisplay); queue.swap(pending);
        // Resident FINAL chunks outside preload are immediately retired;
        // immutable dependency caches remain separately bounded by their LRUs.
        for (auto it = cache.begin(); it != cache.end();) {
            if (!target.count(it->first)) it = cache.erase(it); else ++it;
        }
        invalidate();
    }
    void retarget(int cx, int cz, bool force = false) {
        retarget(cx, cz, radius, preloadRadius, force);
    }
    static void validateBaseline(const ModifiedChunk& changes, const Chunk& chunk) {
        if (changes.formatVersion != 1 && changes.formatVersion != 2) invalid("invalid saved edit schema");
        if (chunk.biomes != changes.biomes) invalid("saved biome mismatch");
        for (const auto& edit : changes.edits) {
            if (edit.first >= ChunkCells || edit.second > legacy::kBlockCount) invalid("invalid saved edit index/id");
            if (changes.formatVersion == 2 && edit.second) {
                const auto& spec = legacy::kBlockSpecs[edit.second - 1];
                if (!std::strcmp(spec.shape, "liquid") || !std::strcmp(spec.shape, "air")) invalid("invalid saved placement");
            }
            const uint16_t before = readCell(chunk.baseline, edit.first);
            if (edit.second == before) invalid("invalid or redundant saved edit");
            if (changes.formatVersion == 1) {
                if (edit.second || !before) invalid("invalid saved mining edit");
                const auto& spec = legacy::kBlockSpecs[before - 1];
                if (!std::strcmp(spec.key, "unbreakable") || !std::strcmp(spec.shape, "liquid") || !std::strcmp(spec.shape, "air"))
                    invalid("saved mining edit targets an unbreakable or nonsolid block");
            } else if (edit.first < 256 || (before && !std::strcmp(legacy::kBlockSpecs[before - 1].key, "unbreakable"))) {
                invalid("saved edit targets bedrock");
            }
        }
    }

    explicit Impl(uint32_t seed, int worldSize, bool enableStructures, bool enableDecoration)
        : size(worldSize), chunks(worldSize / 16), seed(seed), source(int64_t(seed)),
          generator(original_terrain::Config{int64_t(seed), worldSize, 63, false, false, false},
                    [this](int x, int z, int w, int h) { return convert(source.raw(x, z, w, h)); },
                    [this](int x, int z, int w, int h) { return convert(source.zoomed(x, z, w, h)); }) {
        if (enableStructures) {
            original_structures::Options options;
            options.generationBiome = [this](int qx, int qz) { return source.raw(qx, qz, 1, 1).at(0).id; };
            // Whole-village emissions are separate from terrain; keep this small
            // too. There is no eager whole-world block/structure volume.
            options.cacheRegions = 16;
            structures.reset(new original_structures::Generator(int64_t(seed), size,
                [this](int x, int z) { return source.zoomed(x, z, 1, 1).at(0).id; },
                [this](int x, int z) {
                    const int ax = x + size / 2, az = z + size / 2;
                    if (!inside(ax, 0, az, size)) return 0;
                    return int(base(ax / 16, az / 16).ground[size_t(az % 16) * 16 + ax % 16]);
                },
                [this](int x, int y, int z) {
                    const int ax = x + size / 2, az = z + size / 2;
                    if (!inside(ax, y, az, size) || y >= 128) return uint8_t(0);
                    return base(ax / 16, az / 16).tiles[original_terrain::Generator::index(ax % 16, y, az % 16)];
                }, options));
        }
        if (enableDecoration) {
            original_decoration::Options options; options.cachePlans = 128;
            decoration.reset(new original_decoration::Generator(int64_t(seed), size,
                [this](int x, int z) { return source.zoomed(x, z, 1, 1).at(0).id; },
                [this](int x, int z) {
                    const int ax = x + size / 2, az = z + size / 2;
                    if (!inside(ax, 0, az, size)) return 0;
                    const auto& raw = structured(ax / 16, az / 16);
                    for (int y = Height - 1; y >= 0; --y) {
                        const uint8_t tile = raw.tiles[cell(ax % 16, y, az % 16)];
                        if (tile && tile != 8 && tile != 9 && tile != 10 && tile != 11 && tile != 18) return y + 1;
                    }
                    return 0;
                },
                [this](int x, int y, int z) {
                    const int ax = x + size / 2, az = z + size / 2;
                    if (!inside(ax, y, az, size)) return uint8_t(0);
                    return structured(ax / 16, az / 16).tiles[cell(ax % 16, y, az % 16)];
                }, options));
        }
    }

    BaseChunk& base(int cx, int cz) const {
        const int key = cz * chunks + cx;
        auto it = baseCache.find(key);
        if (it != baseCache.end()) { it->second.used = ++clock; return it->second; }
        auto raw = const_cast<original_terrain::Generator&>(generator).generateChunk(cx - chunks / 2, cz - chunks / 2);
        if (raw.tiles.size() != 16 * 128 * 16 || raw.biomes.size() != 256) invalid("malformed original chunk");
        BaseChunk result;
        result.tiles = std::move(raw.tiles);
        for (size_t i = 0; i < 256; ++i) {
            biomeMeta(raw.biomes[i].id); // also reject upstream-unregistered ID 23
            result.biomes[i] = uint8_t(raw.biomes[i].id);
            const int x = int(i % 16), z = int(i / 16);
            for (int y = 127; y >= 0; --y) {
                const uint8_t tile = result.tiles[original_terrain::Generator::index(x, y, z)];
                // BASE generator emits terrain only: liquids/air do not block;
                // leaves excluded for exact getTopSolidBlock callback semantics.
                if (tile && tile != 8 && tile != 9 && tile != 10 && tile != 11 && tile != 18) {
                    result.ground[i] = uint8_t(y + 1); break;
                }
            }
        }
        result.used = ++clock;
        if (baseCache.size() >= BaseCacheLimit) {
            const auto oldest = std::min_element(baseCache.begin(), baseCache.end(), [](const auto& a, const auto& b) { return a.second.used < b.second.used; });
            baseCache.erase(oldest);
        }
        return baseCache.emplace(key, std::move(result)).first->second;
    }

    StructureChunk& structured(int cx, int cz) const {
        const int key = cz * chunks + cx;
        auto it = structureCache.find(key);
        if (it != structureCache.end()) { it->second.used = ++clock; return it->second; }
        StructureChunk result;
        result.tiles.assign(ChunkCells, 0); result.data.assign(ChunkCells, 0);
        const auto& raw = base(cx, cz);
        for (int y = 0; y < 128; ++y) for (int z = 0; z < 16; ++z) for (int x = 0; x < 16; ++x)
            result.tiles[cell(x, y, z)] = raw.tiles[original_terrain::Generator::index(x, y, z)];
        // All BASE bytes have been copied before structure callbacks can evict
        // them. This stage NEVER calls chunk(), decoration, or player overlays.
        if (structures) {
            for (const auto& block : structures->generateForChunk(cx - chunks / 2, cz - chunks / 2)) {
                const int x = block.x + size / 2, z = block.z + size / 2;
                if (!inside(x, block.y, z, size) || x / 16 != cx || z / 16 != cz || block.y == 0)
                    invalid("structure output outside requested chunk");
                const auto at = cell(x % 16, block.y, z % 16);
                result.tiles[at] = block.tile; result.data[at] = block.data;
            }
        }
        result.used = ++clock;
        if (structureCache.size() >= StructureCacheLimit) {
            const auto oldest = std::min_element(structureCache.begin(), structureCache.end(), [](const auto& a, const auto& b) { return a.second.used < b.second.used; });
            structureCache.erase(oldest);
        }
        return structureCache.emplace(key, std::move(result)).first->second;
    }

    Chunk& chunk(int cx, int cz) const {
        const int key = cz * chunks + cx;
        auto it = cache.find(key);
        if (it != cache.end()) { it->second.used = ++clock; return it->second; }
        Chunk result = generateBaseline(cx, cz);
        result.blocks = result.baseline;
        const auto changes = modified.find(key);
        if (changes != modified.end()) {
            // Only saved overlays carry save-format constraints. Live low-level
            // edits are already validated by their own API and must not be
            // rejected here (setBlock may legitimately clear bedrock).
            if (!changes->second.baselineValidated) validateBaseline(changes->second, result);
            for (const auto& edit : changes->second.edits) result.blocks[edit.first] = edit.second;
        }
        if (compact()) trimChunk(result);
        // Allocate the map node before eviction or deferred-validation publication.
        std::map<int, Chunk> staged;
        result.used = clock + 1;
        staged.emplace(key, std::move(result));
        if (cache.size() >= cacheLimit() - BaseCacheLimit - StructureCacheLimit) {
            auto oldest = cache.end();
            for (auto candidate = cache.begin(); candidate != cache.end(); ++candidate) {
                if (streaming && target.count(candidate->first)) continue;
                if (oldest == cache.end() || candidate->second.used < oldest->second.used) oldest = candidate;
            }
            if (oldest != cache.end()) cache.erase(oldest);
        }
        auto inserted = cache.insert(staged.extract(key));
        ++clock;
        if (changes != modified.end() && !changes->second.baselineValidated) {
            changes->second.baselineValidated = true; --deferredCount;
        }
        ++generated;
        if (streaming && displayTarget.count(key)) { windowDirty = true; ++readyRevision; }
        return inserted.position->second;
    }

    Chunk generateBaseline(int cx, int cz) const {
        // Original getMinDistanceToEdge uses [-worldSize/2,worldSize/2-1].
        // Shift app absolute [0,size) to original [-size/2,size/2).
        Chunk result;
        result.biomes = base(cx, cz).biomes;
        result.baseline.assign(ChunkCells, 0);
        const auto& raw = structured(cx, cz);
        std::array<uint16_t, 4096> mapping {};
        std::array<bool, 4096> mapped {};
        for (size_t at = 0; at < ChunkCells; ++at) {
            const auto tile = raw.tiles[at], data = raw.data[at];
            if (data > 15) invalid("invalid structure metadata");
            const size_t variant = size_t(tile) * 16 + data;
            if (!mapped[variant]) { mapping[variant] = catalogForTile(tile, data); mapped[variant] = true; }
            result.baseline[at] = mapping[variant];
        }
        // Copy before decoration's BASE+structures callbacks evict structured().
        // No generation stage ever reads edits or recursively enters chunk().
        if (decoration) {
            for (const auto& block : decoration->generateForChunk(cx - chunks / 2, cz - chunks / 2)) {
                const int x = block.x + size / 2, z = block.z + size / 2;
                if (!inside(x, block.y, z, size) || x / 16 != cx || z / 16 != cz || block.y == 0)
                    invalid("decoration output outside requested chunk");
                result.baseline[cell(x % 16, block.y, z % 16)] = catalogForTile(block.tile, block.data);
            }
        }
        return result;
    }
};

bool OriginalWorld::validSize(int size) { return size == 864 || size == 1024 || size == 3072 || size == 5120; }
OriginalWorld::OriginalWorld(uint32_t seed, int size, bool structures, bool decoration) {
    if (!validSize(size)) invalid("unsupported world size");
    impl_.reset(new Impl(seed, size, structures, decoration));
}
int OriginalWorld::size() const { return impl_->size; }
bool OriginalWorld::structuresEnabled() const { return bool(impl_->structures); }
bool OriginalWorld::decorationsEnabled() const { return bool(impl_->decoration); }
std::vector<OriginalWorld::Landmark> OriginalWorld::landmarks(int x, int z, int radius) const {
    if (!inside(x, 0, z, impl_->size) || radius < 0 || radius > impl_->size) invalid("invalid landmark query");
    std::vector<Landmark> result;
    if (!impl_->structures) return result;
    const int offset = impl_->size / 2;
    for (const auto& v : impl_->structures->nearby(x - offset, z - offset, radius))
        result.push_back({v.id, v.type, v.sourceX + offset, v.sourceZ + offset, v.biome,
                          v.minX + offset, v.minZ + offset, v.maxX + offset, v.maxZ + offset, v.pieceCount});
    return result;
}
OriginalWorld::~OriginalWorld() = default;
void OriginalWorld::setRenderRadius(int radius) {
    if (radius < 1 || radius > MaxRadius) invalid("render radius must be in 1..12");
    setChunkRanges(radius, radius);
}
// Window, FINAL cache and lighting all grow with the radius. Measure what the
// system actually has free and pick the largest radius that fits, so a high
// setting degrades gracefully instead of exhausting memory mid-play.
int OriginalWorld::affordableRadius(int requested) {
    requested = std::clamp(requested, 1, MaxRadius);
    static const size_t availableBytes = [] {
        // Hand-rolled parsing on purpose: sscanf/strtol pull in
        // __isoc23_* symbols from GLIBC 2.38, which the device cannot resolve.
        size_t kib = 0;
        if (FILE* file = std::fopen("/proc/meminfo", "re")) {
            char line[256];
            while (std::fgets(line, sizeof line, file)) {
                const char* label = "MemAvailable:";
                size_t i = 0;
                while (label[i] && line[i] == label[i]) ++i;
                if (label[i]) continue;
                size_t value = 0;
                bool digits = false;
                for (const char* c = line + i; *c; ++c) {
                    if (*c >= '0' && *c <= '9') { value = value * 10 + size_t(*c - '0'); digits = true; }
                    else if (digits) break;
                }
                if (digits) kib = value;
                break;
            }
            std::fclose(file);
        }
        return kib * 1024;
    }();
    if (!availableBytes) return requested; // Unknown budget: honour the request.
    // Keep a margin for the renderer, QuickJS, textures and the host process.
    const size_t budget = availableBytes > (96u << 20) ? availableBytes - (96u << 20) : availableBytes / 2;
    for (int radius = requested; radius > 1; --radius) {
        const size_t side = size_t(radius) * 2 + 1;
        const size_t window = side * 16 * Height * side * 16 * sizeof(uint16_t);
        const size_t chunks = side * side * size_t(ChunkCells) * sizeof(uint16_t) * 2;
        const size_t lighting = window; // two byte planes, same cell count
        if (window + chunks + lighting <= budget) return radius;
    }
    return 1;
}
void OriginalWorld::setChunkRanges(int displayRadius, int preloadRadius) {
    if (displayRadius < 1 || displayRadius > MaxRadius || preloadRadius < displayRadius || preloadRadius > MaxRadius)
        invalid("chunk ranges must satisfy 1 <= display <= preload <= 12");
    // Clamp to what memory can actually hold. A radius the device cannot fit
    // would otherwise die in the allocator mid-play; streamingInfo then reports
    // the honest applied value rather than pretending the request took effect.
    preloadRadius = affordableRadius(preloadRadius);
    displayRadius = std::min(displayRadius, preloadRadius);
    auto& p = *impl_;
    if (p.radius == displayRadius && p.preloadRadius == preloadRadius) return;
    // Prepare compaction before changing ranges/epoch. Allocation failure leaves
    // the old live configuration and vectors intact.
    std::map<int, Impl::Chunk> compacted;
    if (preloadRadius > 3 && !p.compact()) {
        for (const auto& entry : p.cache) {
            auto& chunk = compacted.emplace(entry.first, entry.second).first->second;
            Impl::trimChunk(chunk);
        }
    }
    if (p.streaming && p.centerX >= 0)
        p.retarget(p.centerX, p.centerZ, displayRadius, preloadRadius, true);
    else {
        p.radius = displayRadius; p.preloadRadius = preloadRadius;
        p.invalidate();
    }
    for (auto& entry : compacted) {
        const auto resident = p.cache.find(entry.first);
        if (resident != p.cache.end()) {
            resident->second.baseline.swap(entry.second.baseline);
            resident->second.blocks.swap(entry.second.blocks);
        }
    }
}
void OriginalWorld::beginStreaming(int radius) {
    setRenderRadius(radius);
    auto& p = *impl_;
    if (p.streaming) return;
    p.streaming = true;
    if (p.centerX >= 0) p.retarget(p.centerX, p.centerZ, true);
    else p.invalidate();
}
void OriginalWorld::endStreaming() {
    auto& p = *impl_;
    if (!p.streaming) return;
    p.streaming = false; p.epoch = nextEpoch(); p.target.clear(); p.displayTarget.clear(); p.queue.clear(); p.invalidate();
}
bool OriginalWorld::streaming() const { return impl_->streaming; }
void OriginalWorld::setStreamingCenter(float x, float z) {
    if (!std::isfinite(x) || !std::isfinite(z) || x < 0 || x >= impl_->size || z < 0 || z >= impl_->size)
        invalid("invalid streaming center");
    auto& p = *impl_;
    const bool moved = p.eyeX != x || p.eyeZ != z;
    p.eyeX = x; p.eyeZ = z;
    p.retarget(int(x) / 16, int(z) / 16);
    if (moved && p.hasView) p.orderQueue();
}
void OriginalWorld::setStreamingView(float yaw, float pitch, float aspect) {
    if (!std::isfinite(yaw) || !std::isfinite(pitch) || !std::isfinite(aspect) || aspect <= 0)
        invalid("invalid streaming view");
    auto& p = *impl_;
    yaw = std::remainder(yaw, 360.0f); pitch = std::remainder(pitch, 360.0f);
    if (p.hasView && p.yaw == yaw && p.pitch == pitch && p.aspect == aspect) return;
    p.yaw = yaw; p.pitch = pitch; p.aspect = aspect; p.hasView = true;
    p.orderQueue(); // No target/epoch/revision change and no FINAL reads.
}
std::vector<ChunkRequest> OriginalWorld::chunkRequests(int budget) const {
    if (budget < 0) invalid("negative chunk load budget");
    const auto& p = *impl_;
    std::vector<ChunkRequest> result;
    if (!p.streaming) return result;
    result.reserve(std::min(size_t(budget), p.queue.size()));
    for (int key : p.queue) {
        if (result.size() >= size_t(budget)) break;
        if (!p.target.count(key) || p.cache.count(key)) continue;
        result.push_back({p.epoch, p.seed, p.size, key, bool(p.structures), bool(p.decoration)});
    }
    return result;
}
GeneratedChunk OriginalWorld::generateChunk(const ChunkRequest& request,
                                          std::unique_ptr<OriginalWorld>& context) {
    if (!request.epoch || !validSize(request.worldSize) || request.key < 0 ||
        request.key >= (request.worldSize / 16) * (request.worldSize / 16)) invalid("invalid chunk request");
    const auto start = std::chrono::steady_clock::now();
    if (!context || !context->impl_->isolated || context->impl_->seed != request.seed ||
        context->size() != request.worldSize || context->structuresEnabled() != request.structures ||
        context->decorationsEnabled() != request.decoration) {
        auto next = std::make_unique<OriginalWorld>(request.seed, request.worldSize, request.structures, request.decoration);
        next->impl_->isolated = true;
        context = std::move(next);
    }
    // A reused context retains only its own generator/dependency caches. Never
    // use chunk(): FINAL cache, modified overlays and render state are irrelevant.
    auto& p = *context->impl_;
    auto baseline = p.generateBaseline(request.key % p.chunks, request.key / p.chunks);
    GeneratedChunk result;
    result.request = request; result.baseline = std::move(baseline.baseline); result.biomes = baseline.biomes;
    result.loadMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return result;
}
bool OriginalWorld::commitChunk(GeneratedChunk&& generated) {
    auto& p = *impl_;
    const auto& r = generated.request;
    // Stale results are a normal no-op, before inspecting or allocating payloads.
    if (!p.streaming || r.epoch != p.epoch || r.seed != p.seed || r.worldSize != p.size ||
        r.structures != bool(p.structures) || r.decoration != bool(p.decoration) ||
        r.key < 0 || r.key >= p.chunks * p.chunks || !p.target.count(r.key) || p.cache.count(r.key)) return false;
    if (generated.baseline.size() != ChunkCells || !std::isfinite(generated.loadMs) || generated.loadMs < 0)
        invalid("malformed generated chunk");
    for (uint16_t block : generated.baseline) if (block > legacy::kBlockCount) invalid("invalid generated catalog id");
    for (uint8_t biome : generated.biomes) biomeMeta(biome);
    Impl::Chunk chunk;
    chunk.baseline = std::move(generated.baseline); chunk.biomes = generated.biomes;
    chunk.blocks = chunk.baseline;
    const auto changes = p.modified.find(r.key);
    if (changes != p.modified.end()) {
        // Revalidate the latest overlay, not a worker/start-time snapshot. Only
        // saved overlays carry the save-format constraints; live edits made while
        // the worker ran are already validated and merged as-is.
        if (!changes->second.baselineValidated) Impl::validateBaseline(changes->second, chunk);
        for (const auto& edit : changes->second.edits) chunk.blocks[edit.first] = edit.second;
    }
    if (p.compact()) Impl::trimChunk(chunk);
    chunk.used = p.clock + 1;
    std::map<int, Impl::Chunk> staged;
    auto& ready = staged.emplace(r.key, std::move(chunk)).first->second;
    // Publication must be O(one chunk column), never a whole-window copy: a
    // radius-three window is ~6 MB and copying it per commit is a visible stall.
    // The window is only written in place AFTER the allocating insert below.
    const int cx = r.key % p.chunks, cz = r.key / p.chunks;
    const int ox = p.volume.originX, oz = p.volume.originZ;
    const int x0 = std::max(ox, cx * 16), x1 = std::min(ox + p.volume.width, cx * 16 + 16);
    const int z0 = std::max(oz, cz * 16), z1 = std::min(oz + p.volume.depth, cz * 16 + 16);
    const bool visible = !p.windowDirty && !p.window.empty() && x0 < x1 && z0 < z1 &&
                         p.loadedColumns.size() == size_t(p.volume.width) * p.volume.depth;
    const bool needsHeight = visible && p.chunkHeight(r.key, ready) > p.volume.height;
    // No allocation or throwing work after this point. The dynamic FINAL limit
    // holds all targets (up to 625); queue erase cannot allocate.
    p.cache.insert(staged.extract(r.key));
    if (visible && !needsHeight) {
        // Preload chunks outside the display window may have x0 > x1 or z0 >
        // z1. Never form row pointers or mask ranges until both intersections
        // are nonempty. Hidden commits must not trigger display relighting.
        for (int z = z0; z < z1; ++z)
            std::fill_n(p.loadedColumns.begin() + std::ptrdiff_t(size_t(z - oz) * p.volume.width + x0 - ox),
                        x1 - x0, uint8_t(1));
        for (int y = 0; y < p.volume.height; ++y) for (int z = z0; z < z1; ++z) {
            auto* row = &p.window[(size_t(y) * p.volume.depth + z - oz) * p.volume.width + x0 - ox];
            const size_t source = cell(x0 - cx * 16, y, z - cz * 16);
            for (int x = 0; x < x1 - x0; ++x) {
                const uint16_t value = Impl::readCell(ready.blocks, source + x);
                p.nonair += int(value != 0) - int(row[x] != 0);
                row[x] = value;
            }
        }
    }
    if (changes != p.modified.end() && !changes->second.baselineValidated) {
        changes->second.baselineValidated = true; --p.deferredCount;
    }
    p.queue.erase(std::remove(p.queue.begin(), p.queue.end(), r.key), p.queue.end());
    ++p.clock; ++p.generated; p.lastLoadMs = generated.loadMs;
    if (needsHeight) p.invalidate();
    else if (visible) ++p.readyRevision;
    // A missing/stale window remains dirty and will copy all current display
    // residents on its next rebuild; hidden cache progress is not a render edit.
    return true;
}
bool OriginalWorld::chunkLoaded(int x, int z) const {
    return inside(x, 0, z, impl_->size) && impl_->cache.count((z / 16) * impl_->chunks + x / 16);
}
bool OriginalWorld::loadChunk(int x, int z) {
    if (!inside(x, 0, z, impl_->size)) invalid("load outside world");
    if (chunkLoaded(x, z)) return false;
    impl_->chunk(x / 16, z / 16);
    auto& q = impl_->queue;
    const int key = (z / 16) * impl_->chunks + x / 16;
    q.erase(std::remove(q.begin(), q.end(), key), q.end());
    return true;
}
int OriginalWorld::processChunkLoads(int budget) {
    if (budget < 0) invalid("negative chunk load budget");
    auto& p = *impl_;
    if (!p.streaming || !budget || p.queue.empty()) return 0;
    const auto start = std::chrono::steady_clock::now();
    int done = 0;
    try {
        while (done < budget && !p.queue.empty()) {
            const int key = p.queue.front();
            // Remove only after successful commit so exceptions remain visible.
            if (!p.cache.count(key)) { p.chunk(key % p.chunks, key / p.chunks); ++done; }
            p.queue.erase(p.queue.begin());
        }
    } catch (...) {
        p.lastLoadMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        throw;
    }
    p.lastLoadMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return done;
}
StreamingInfo OriginalWorld::streamingInfo() const {
    const auto& p = *impl_;
    StreamingInfo info;
    info.enabled = p.streaming; info.radius = p.radius; info.preloadRadius = p.preloadRadius;
    // View distance must follow the window that is actually published, not the
    // requested radius: looking past a smaller resident window is what exposes
    // the unloaded boundary as a visible wall.
    const int visible = p.radius ? (p.windowRadius >= 0 ? p.windowRadius : p.radius) : 0;
    info.windowSize = p.radius ? (visible * 2 + 1) * 16 : p.windowSize();
    info.maxDistance = p.radius ? std::max(8, visible * 16 - 8) : 40;
    info.queued = int(p.queue.size()); info.target = int(p.target.size()); info.resident = int(p.cache.size());
    info.displayTarget = int(p.displayTarget.size());
    for (int key : p.target) info.loaded += p.cache.count(key) != 0;
    for (int key : p.displayTarget) info.displayLoaded += p.cache.count(key) != 0;
    info.centerChunkX = p.centerX; info.centerChunkZ = p.centerZ; info.totalGenerated = p.generated;
    info.baseCached = int(p.baseCache.size()); info.structureCached = int(p.structureCache.size());
    info.deferred = p.deferredCount;
    info.cacheLimit = p.cacheLimit();
    for (const auto& entry : p.cache)
        info.cacheBytes += sizeof(uint16_t) * (entry.second.baseline.capacity() + entry.second.blocks.capacity()) + sizeof(entry.second);
    for (const auto& entry : p.baseCache) info.cacheBytes += entry.second.tiles.capacity() + sizeof(entry.second);
    for (const auto& entry : p.structureCache)
        info.cacheBytes += entry.second.tiles.capacity() + entry.second.data.capacity() + sizeof(entry.second);
    info.windowBytes = sizeof(uint16_t) * p.window.capacity() + p.loadedColumns.capacity();
    info.lastLoadMs = p.lastLoadMs;
    return info;
}
uint64_t OriginalWorld::streamingRevision() const { return impl_->readyRevision; }
uint16_t OriginalWorld::blockAt(int x, int y, int z) const {
    if (!inside(x, y, z, impl_->size) || (impl_->streaming && !chunkLoaded(x, z))) return 0;
    return Impl::readCell(impl_->chunk(x / 16, z / 16).blocks, cell(x % 16, y, z % 16));
}
uint8_t OriginalWorld::biomeAt(int x, int z) const {
    if (!inside(x, 0, z, impl_->size)) invalid("biome outside world");
    // Reuse resident bytes for the HUD, but never force a whole structure/terrain
    // chunk just to name a distant biome. No extra whole-world biome cache.
    const int key = (z / 16) * impl_->chunks + x / 16;
    const size_t at = size_t(z % 16) * 16 + x % 16;
    const auto cached = impl_->cache.find(key);
    if (cached != impl_->cache.end()) return cached->second.biomes[at];
    const auto base = impl_->baseCache.find(key);
    if (base != impl_->baseCache.end()) return base->second.biomes[at];
    const auto biome = impl_->source.zoomed(x - impl_->size / 2, z - impl_->size / 2, 1, 1).at(0).id;
    biomeMeta(biome);
    return uint8_t(biome);
}
bool OriginalWorld::setBlock(int x, int y, int z, uint16_t value) {
    return setBlocks({{x, y, z, value}});
}
bool OriginalWorld::setBlocks(const std::vector<BlockChange>& changes) {
    auto& p = *impl_;
    std::set<std::pair<int, uint32_t>> cells;
    for (const auto& change : changes) {
        if (!inside(change.x, change.y, change.z, p.size) || change.value > legacy::kBlockCount)
            invalid("invalid block edit");
        const int key = (change.z / 16) * p.chunks + change.x / 16;
        const auto at = uint32_t(cell(change.x % 16, change.y, change.z % 16));
        if (!cells.emplace(key, at).second) invalid("duplicate batch cell");
        if (p.streaming && !chunkLoaded(change.x, change.z)) return false;
    }
    if (changes.empty()) return false;
    // Prepare a complete overlay copy. Chunk generation/LRU eviction can throw,
    // but no edited terrain is published until every allocation has succeeded.
    auto next = p.modified;
    bool changed = false;
    for (const auto& change : changes) {
        auto& chunk = p.chunk(change.x / 16, change.z / 16);
        const int key = (change.z / 16) * p.chunks + change.x / 16;
        const auto at = uint32_t(cell(change.x % 16, change.y, change.z % 16));
        // chunk() may validate a previously deferred overlay; retain that state.
        const auto existing = p.modified.find(key);
        if (existing != p.modified.end()) next.at(key).baselineValidated = existing->second.baselineValidated;
        if (Impl::readCell(chunk.blocks, at) == change.value) continue;
        changed = true;
        auto it = next.find(key);
        if (it == next.end()) {
            ModifiedChunk entry; entry.biomes = chunk.biomes; entry.baselineValidated = true;
            it = next.emplace(key, std::move(entry)).first;
        }
        if (change.value == Impl::readCell(chunk.baseline, at)) it->second.edits.erase(at);
        else it->second.edits[at] = change.value;
        it->second.formatVersion = 2; it->second.dirty = true;
    }
    if (!changed) return false;
    // Compact chunks have their upper air trimmed. A block placed above the
    // stored height used to be written past the end of the vector. Grow the
    // resident vectors here, after every p.chunk() call (no further eviction)
    // and before the swap, so an allocation failure leaves state untouched.
    for (const auto& change : changes) {
        if (!change.value) continue;
        const auto resident = p.cache.find((change.z / 16) * p.chunks + change.x / 16);
        const size_t at = cell(change.x % 16, change.y, change.z % 16);
        if (resident != p.cache.end() && resident->second.blocks.size() <= at)
            resident->second.blocks.resize(at + 1, 0);
    }
    // swap/find and writes to existing vectors cannot allocate or throw. Do not
    // keep Chunk references across preparation: LRU may evict an earlier chunk.
    p.modified.swap(next);
    for (const auto& change : changes) {
        const int key = (change.z / 16) * p.chunks + change.x / 16;
        const auto resident = p.cache.find(key);
        const size_t at = cell(change.x % 16, change.y, change.z % 16);
        // Air past the trimmed end is already implied; only stored cells are written.
        if (resident != p.cache.end() && at < resident->second.blocks.size())
            resident->second.blocks[at] = change.value;
        const int wx = change.x - p.volume.originX, wz = change.z - p.volume.originZ;
        if (!p.window.empty() && wx >= 0 && wx < p.volume.width && wz >= 0 && wz < p.volume.depth) {
            auto& previous = p.window[(size_t(change.y) * p.volume.depth + wz) * p.volume.width + wx];
            p.nonair += int(change.value != 0) - int(previous != 0); previous = change.value;
        }
    }
    return true;
}
const RenderVolume& OriginalWorld::renderVolume(float playerX, float playerZ, float playerY) const {
    (void)playerY; // Accepted for the trimmed-height window; still full height.
    if (!std::isfinite(playerX) || !std::isfinite(playerZ) || playerX < 0 || playerX >= impl_->size || playerZ < 0 || playerZ >= impl_->size)
        invalid("invalid window center");
    auto& p = *impl_;
    const int cx = int(playerX) / 16, cz = int(playerZ) / 16;
    const bool moved = p.eyeX != playerX || p.eyeZ != playerZ;
    p.eyeX = playerX; p.eyeZ = playerZ;
    p.retarget(cx, cz);
    if (moved && p.hasView) p.orderQueue();
    // Grow the window only as far as terrain is resident, and rebuild whenever
    // that frontier advances, so a raised radius fills in instead of revealing
    // an abrupt unloaded border.
    const int visible = p.residentRadius(cx, cz);
    if (!p.windowDirty && p.playerChunkX == cx && p.playerChunkZ == cz && p.windowRadius == visible)
        return p.volume;
    p.windowRadius = visible;
    // Preserve the old 96 window exactly. Odd-chunk radius windows are aligned;
    // their worst-case margin is radius*16 (ray uses radius*16-8).
    const int size = p.radius ? (visible * 2 + 1) * 16 : 96;
    const int ox = std::clamp(p.radius ? (cx - visible) * 16 : cx * 16 + 8 - 48, 0, p.size - size);
    const int oz = std::clamp(p.radius ? (cz - visible) * 16 : cz * 16 + 8 - 48, 0, p.size - size);
    std::vector<uint16_t> next(size_t(size) * Height * size, 0);
    // Record which columns are backed by a resident chunk. Unknown columns stay
    // zero so lighting can tell "no terrain loaded here" apart from "real air".
    std::vector<uint8_t> loaded(size_t(size) * size, 0);
    int nonair = 0;
    for (int zc = oz / 16; zc <= (oz + size - 1) / 16; ++zc) for (int xc = ox / 16; xc <= (ox + size - 1) / 16; ++xc) {
        const int key = zc * p.chunks + xc;
        const auto resident = p.cache.find(key);
        if (p.streaming && resident == p.cache.end()) continue; // UNKNOWN, never generate on a read
        const auto& chunk = p.streaming ? resident->second : p.chunk(xc, zc);
        const int x0 = std::max(ox, xc * 16), x1 = std::min(ox + size, xc * 16 + 16);
        const int z0 = std::max(oz, zc * 16), z1 = std::min(oz + size, zc * 16 + 16);
        for (int z = z0; z < z1; ++z)
            std::fill_n(loaded.begin() + std::ptrdiff_t(size_t(z - oz) * size + x0 - ox), x1 - x0, uint8_t(1));
        // With preload > 3 (compact()), resident chunks have their upper air
        // trimmed, so blocks.size() can be far below ChunkCells. Copying a full
        // 256-high column read past the vector into heap memory, which showed
        // up as floating "ghost" terrain once the range was raised. Cells past
        // the stored size are air: `next` is already zero-filled.
        const size_t stored = chunk.blocks.size();
        for (int y = 0; y < Height; ++y) for (int z = z0; z < z1; ++z) {
            const size_t from = cell(x0 - xc * 16, y, z - zc * 16);
            if (from >= stored) continue;
            const size_t count = std::min(size_t(x1 - x0), stored - from);
            const auto begin = chunk.blocks.begin() + std::ptrdiff_t(from);
            const auto end = begin + std::ptrdiff_t(count);
            std::copy(begin, end, next.begin() + std::ptrdiff_t((size_t(y) * size + z - oz) * size + x0 - ox));
            nonair += int(std::count_if(begin, end, [](uint16_t v) { return v != 0; }));
        }
    }
    p.window.swap(next); p.loadedColumns.swap(loaded); p.nonair = nonair;
    p.volume = {p.window.data(), ox, 0, oz, size, Height, size, p.loadedColumns.data()};
    p.playerChunkX = cx; p.playerChunkZ = cz; p.windowDirty = false;
    return p.volume;
}
size_t OriginalWorld::cachedChunkCount() const { return impl_->cache.size() + impl_->baseCache.size() + impl_->structureCache.size(); }
int OriginalWorld::windowBlockCount(float x, float z, float playerY) const {
    renderVolume(x, z, playerY);
    return impl_->nonair;
}
const std::map<int, OriginalWorld::ModifiedChunk>& OriginalWorld::modifiedChunks() const { return impl_->modified; }
void OriginalWorld::restoreChunk(int key, ModifiedChunk changes, bool deferBaseline) {
    auto& p = *impl_;
    if (key < 0 || key >= p.chunks * p.chunks || p.modified.count(key) ||
        (changes.formatVersion != 1 && changes.formatVersion != 2) ||
        (changes.formatVersion == 1 && changes.edits.empty())) invalid("invalid or duplicate saved chunk");
    // Validate schema/catalog and exact recorded biomes WITHOUT terrain/structure
    // generation. Only edit-vs-baseline checks are deferred to first FINAL load.
    const auto biomes = p.source.zoomed((key % p.chunks) * 16 - p.size / 2,
                                      (key / p.chunks) * 16 - p.size / 2, 16, 16);
    for (size_t i = 0; i < changes.biomes.size(); ++i) {
        biomeMeta(changes.biomes[i]);
        if (biomes.at(i).id != changes.biomes[i]) invalid("saved biome mismatch");
    }
    for (const auto& edit : changes.edits) {
        if (edit.first >= ChunkCells || edit.second > legacy::kBlockCount || (changes.formatVersion == 1 && edit.second))
            invalid("invalid saved edit index/id");
        if (changes.formatVersion == 2 && edit.second) {
            const auto& spec = legacy::kBlockSpecs[edit.second - 1];
            if (!std::strcmp(spec.shape, "liquid") || !std::strcmp(spec.shape, "air"))
                invalid("saved placement must be a nonliquid catalog block");
        }
    }
    changes.baselineValidated = false;
    if (!deferBaseline) {
        const auto& chunk = p.chunk(key % p.chunks, key / p.chunks);
        Impl::validateBaseline(changes, chunk);
        changes.baselineValidated = true;
    }
    const bool pending = !changes.baselineValidated;
    p.modified.emplace(key, std::move(changes));
    p.deferredCount += pending;
    p.cache.erase(key); // No FINAL visibility before overlay validation.
    if (p.streaming && p.centerX >= 0) p.retarget(p.centerX, p.centerZ, true);
    else p.invalidate();
}
void OriginalWorld::markPersisted(int key, uint32_t checksum) {
    auto& chunk = impl_->modified.at(key); chunk.persistedHash = checksum; chunk.persisted = true; chunk.dirty = false;
}
} // namespace mcworld
