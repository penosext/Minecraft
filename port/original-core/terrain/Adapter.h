#pragma once

// The existing original_core global Random/PerlinNoise and array template are
// shared, not redefined. Every game-facing shell below is namespace-isolated.
#include "PlatformPch.h"
#include "PerlinNoise.h"
#include "Generator.h"
#include "TerrainExtracted.h"

#include <cmath>
#include <deque>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#if !defined(_LARGE_WORLDS) || defined(__PS3__)
#error "Terrain is audited for _LARGE_WORLDS=1, native CPU only, not PS3/SPU"
#endif

namespace original_terrain_detail {

// System.h's global counter is an int64_t alias for Random.cpp. This separate
// namespace-local shell supplies the original terrain .QuadPart interface.
union LARGE_INTEGER {
    struct { std::uint32_t LowPart; std::int32_t HighPart; };
    std::int64_t QuadPart;
};
inline int QueryPerformanceCounter(LARGE_INTEGER* counter) noexcept
{
    return ::QueryPerformanceCounter(&counter->QuadPart);
}

class Biome {
public:
    int id;
    float depth, scale, temperature;
    byte topMaterial, material;
    float getTemperature(); // definition extracted byte-for-byte
};

// Definitions are extracted original aliases; the existing template has no
// destructor and retains its shallow-pointer, manually-freed ownership model.
#include "TerrainArrays.inc"

class BiomeSource {
public:
    BiomeSource(original_terrain::BiomeQuery raw, original_terrain::BiomeQuery zoomed)
        : raw_(std::move(raw)), zoomed_(std::move(zoomed)) {}
    void getRawBiomeBlock(BiomeArray& out, int x, int z, int w, int h);
    void getBiomeBlock(BiomeArray& out, int x, int z, int w, int h, bool cache);
    const std::vector<original_terrain::BiomeData>& lastZoomed() const { return lastZoomed_; }
    void clear() { storage_.clear(); lastZoomed_.clear(); }
private:
    void query(BiomeArray& out, const original_terrain::BiomeQuery& fn,
               int x, int z, int w, int h, bool zoomed);
    original_terrain::BiomeQuery raw_, zoomed_;
    std::deque<std::vector<Biome>> storage_; // raw and zoomed pointers stay stable
    std::vector<original_terrain::BiomeData> lastZoomed_;
};

class LevelData {
public:
    explicit LevelData(const original_terrain::Config& config) : config_(config) {}
    int getXZSize() const { return config_.worldSize / 16; }
    void getMoatFlags(bool* classic, bool* small, bool* medium) const
    {
        *classic = config_.classicEdgeMoat;
        *small = config_.smallEdgeMoat;
        *medium = config_.mediumEdgeMoat;
    }
private:
    original_terrain::Config config_;
};

class Level {
public:
#include "TerrainLevelConstants.inc"
    Level(const original_terrain::Config& config, BiomeSource* source)
        : seaLevel(config.seaLevel), seed(config.seed), data_(config), source_(source) {}
    int seaLevel;
    const std::int64_t seed;
    int getSeaLevel() const { return seaLevel; }
    LevelData* getLevelData() { return &data_; }
    BiomeSource* getBiomeSource() { return source_; }
private:
    LevelData data_;
    BiomeSource* source_;
};

// The original branches remain present, but this boundary ALWAYS returns null.
// This is upstream default/no overrides, not an implementation of game options.
class LevelGenerationOptions {
public:
    void getBiomeOverride(int, byte&, byte&) { std::terminate(); }
};
struct DefaultOptionsBoundary {
    LevelGenerationOptions* getLevelGenerationOptions() const { return nullptr; }
};
extern DefaultOptionsBoundary app;

// Only the Vec3 temporary allocation/TLS boundary is adapted; newTemp, set,
// dot, length and both line geometry definitions are extracted unchanged.
class Vec3 {
    class ThreadStorage {
    public:
        static const int POOL_SIZE = 1024;
        Vec3* pool;
        unsigned int poolPointer;
        ThreadStorage();
        ~ThreadStorage();
    };
    static const unsigned int tlsIdx = 0;
    static void* TlsGetValue(unsigned int)
    {
        static thread_local ThreadStorage storage;
        return &storage;
    }
    Vec3() = default;
    Vec3* set(double x, double y, double z);
public:
    double x, y, z;
    static Vec3* newTemp(double x, double y, double z);
    double dot(Vec3* p);
    double length();
    Vec3* closestPointOnLine(Vec3* p1, Vec3* p2);
    double distanceFromLine(Vec3* p1, Vec3* p2);
};

class RandomLevelSource {
public:
    static const bool FLOATING_ISLANDS = false;
    static const int CHUNK_HEIGHT = 8;
    static const int CHUNK_WIDTH = 4;

    // NOT the upstream full constructor: no feature allocation or execution.
    RandomLevelSource(Level* level, std::int64_t seed);
    ~RandomLevelSource();
    RandomLevelSource(const RandomLevelSource&) = delete;
    RandomLevelSource& operator=(const RandomLevelSource&) = delete;

    int getMinDistanceToEdge(int xxx, int zzz, int worldSize, float falloffStart);
    float getHeightFalloff(int xxx, int zzz, int* pEMin);
    doubleArray getHeights(doubleArray buffer, int x, int y, int z,
                           int xSize, int ySize, int zSize, BiomeArray& biomes);
    void prepareHeights(int xOffs, int zOffs, byteArray blocks);
    void buildSurfaces(int xOffs, int zOffs, byteArray blocks, BiomeArray biomes);
    void seedChunk(int xOffs, int zOffs);

    // Read-only test probes into the actual state, not alternate algorithms.
    double noiseProbe(int n, double x, double y, double z);
    double nextRandomForTest() { return random->nextDouble(); }
    bool powsInitialized() const { return pows.data != nullptr; }

private:
    Random *random = nullptr, *pprandom = nullptr;
    PerlinNoise *lperlinNoise1 = nullptr, *lperlinNoise2 = nullptr;
    PerlinNoise *perlinNoise1 = nullptr, *perlinNoise3 = nullptr;
    PerlinNoise *scaleNoise = nullptr, *depthNoise = nullptr;
    PerlinNoise *floatingIslandScale = nullptr, *floatingIslandNoise = nullptr;
    PerlinNoise *forestNoise = nullptr;
    Level* level = nullptr;
    int m_XZSize = 0;
    bool m_classicEdgeMoat = false, m_smallEdgeMoat = false, m_mediumEdgeMoat = false;
    floatArray pows; // original empty default; getHeights initializes exact pows
};

} // namespace original_terrain_detail
