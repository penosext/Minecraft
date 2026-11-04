#pragma once
// Platform adapter for the extracted carver/ore sources. NEW code: types,
// Level/Biome boundary, no-op 4J hooks. Generation logic lives only in
// generated/Original.cpp (exact upstream bytes).
#include "Carvers.h"
#include "PlatformPch.h"
#include <string>
#define PIXBeginNamedEvent(...) ((void)0)
#define PIXEndNamedEvent(...) ((void)0)

namespace original_carvers { namespace detail {
#include "Pi.inc"
#include "ByteArray.inc"
using ::Random;
using std::wstring;
// 4J terrain-feature registry (ravine markers for the debug map): no-op.
enum eTerrainFeature { eTerrainFeature_Ravine };
struct LevelGenerationOptions {
    bool checkIntersects(int,int,int,int,int,int) { return false; }
};
struct App {
    LevelGenerationOptions* getLevelGenerationOptions() { return nullptr; }
    void AddTerrainFeaturePosition(eTerrainFeature,int,int) {}
};
extern App app;
struct Mth {
    static float *_sin;
    static const float sinScale;
    static void init();
    static float sin(float);
    static float cos(float);
    static int floor(float);
    static int floor(double);
};
struct Tile {
#include "TileConstants.inc"
#include "TileUpdateFlags.inc"
};
struct Biome { byte topMaterial = 0; };
class ChunkSource { public: virtual ~ChunkSource() {} };

// Two adapter modes, never both: CARVE (one 16x128x16 x-z-y chunk and its
// per-block biome topMaterial) and ORES (dense 32x256x32 pre-decoration region
// covering origin chunk xt..xt+1, zt..zt+1 plus own writes).
class Level {
public:
#include "LevelDepth.inc"
    static const int maxBuildHeight = 256;
    explicit Level(int64_t seed) : seed_(seed) {}
    int64_t getSeed() const { return seed_; }
    int seaLevel = 63; // Level::constSeaLevel for useNewSeaLevel worlds
    int getSeaLevel() { return seaLevel; }
    void setInstaTick(bool) {}
    // CARVE mode
    void bindChunk(int cx, int cz, const uint8_t* top);
    Biome* getBiome(int x, int z);
    // ORES mode
    void bindRegion(int xt, int zt, const ChunkTiles& tiles);
    int getTile(int x, int y, int z);
    bool setTileAndData(int x, int y, int z, int tile, int data, int flags);
    int getHeightmap(int x, int z);
    std::vector<Block> takeWrites();
private:
    bool local(int& x, int y, int& z) const;
    int64_t seed_;
    int chunkX_ = 0, chunkZ_ = 0;
    const uint8_t* top_ = nullptr;
    Biome biome_;
    int x0_ = 0, z0_ = 0;
    std::vector<uint8_t> region_;
    std::vector<Block> writes_;
    std::vector<int16_t> heights_;
};
#include "OriginalDeclarations.inc"
class BiomeDecorator {
public:
    BiomeDecorator();
    ~BiomeDecorator();
    Level *level = nullptr;
    Random *random = nullptr;
    int xo = 0, zo = 0;
    Feature *dirtOreFeature, *gravelOreFeature, *coalOreFeature, *ironOreFeature,
            *goldOreFeature, *redStoneOreFeature, *diamondOreFeature, *lapisOreFeature;
    void decorateDepthSpan(int count, Feature *feature, int y0, int y1);
    void decorateDepthAverage(int count, Feature *feature, int yMid, int ySpan);
    void decorateOres();
};
void initialize();
} }
