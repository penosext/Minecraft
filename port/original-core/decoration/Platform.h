#pragma once
#include "Decoration.h"
#include "PlatformPch.h"
#include <map>
#include <tuple>
#include <mutex>
#include <stdexcept>
#define PIXBeginNamedEvent(...) ((void)0)
#define PIXEndNamedEvent(...) ((void)0)
#define PI (3.141592654f)

namespace original_decoration { namespace detail {
using ::Random;
using std::max;
// Rendering-only color labels, irrelevant to Material physics.
enum eMinecraftColour {
    eMinecraftColour_Material_None, eMinecraftColour_Material_Grass,
    eMinecraftColour_Material_Sand, eMinecraftColour_Material_Cloth,
    eMinecraftColour_Material_Fire, eMinecraftColour_Material_Ice,
    eMinecraftColour_Material_Metal, eMinecraftColour_Material_Plant,
    eMinecraftColour_Material_Snow, eMinecraftColour_Material_Clay,
    eMinecraftColour_Material_Dirt, eMinecraftColour_Material_Stone,
    eMinecraftColour_Material_Water, eMinecraftColour_Material_Wood
};
#include "OriginalDeclarations.inc"
struct Mth {
    static float *_sin;
    static const float sinScale;
    static void init();
    static float sin(float);
    static float cos(float);
    static int floor(float);
    static int floor(double);
};
struct Direction {
    static const int SOUTH=0, WEST=1, NORTH=2, EAST=3;
    static const int STEP_X[4], STEP_Z[4], DIRECTION_OPPOSITE[4];
};
struct TreeTile {
#include "TreeTileConstants.inc"
};
struct LeafTile {
#include "LeafTileConstants.inc"
};
struct TallGrass {
#include "TallGrassConstants.inc"
};
struct VineTile {
#include "VineTileConstants.inc"
};
struct LevelGenerationOptions {
    bool checkIntersects(int,int,int,int,int,int) { return false; }
};
struct App {
    LevelGenerationOptions* getLevelGenerationOptions() { return nullptr; }
};
extern App app;
class Tile {
public:
#include "TileConstants.inc"
    static const int UPDATE_CLIENTS=2, UPDATE_ALL=3;
    static Tile* tiles[256];
    static bool solid[256];
    static int lightBlock[256];
    static Tile *reeds, *cactus;
    int id;
    Material* material;
    Tile(int id_, Material* material_) : id(id_), material(material_) {}
    virtual ~Tile() = default;
    virtual bool canSurvive(Level*,int,int,int) { return false; }
};
class Bush : public Tile {
public:
    Bush(int id_) : Tile(id_, Material::plant) {}
    bool mayPlaceOn(int);
    bool canSurvive(Level*,int,int,int) override;
};
class ReedTile : public Tile {
public:
    ReedTile() : Tile(reeds_Id, Material::plant) {}
    bool mayPlace(Level*,int,int,int);
    bool canSurvive(Level*,int,int,int) override;
};
class CactusTile : public Tile {
public:
    CactusTile() : Tile(cactus_Id, Material::cactus) {}
    bool canSurvive(Level*,int,int,int) override;
};
using Position = std::tuple<int,int,int>;
using Writes = std::map<Position,Block>;
struct Dimension { bool hasCeiling=false; };
class Level {
public:
    static const int genDepth=128, genDepthMinusOne=127, maxBuildHeight=256;
    Level(int64_t seed,int size,std::function<int(int,int)> ground,
          std::function<uint8_t(int,int,int)> raw);
    int64_t getSeed() const { return seed_; }
    int getTile(int,int,int);
    bool isEmptyTile(int x,int y,int z) { return getTile(x,y,z)==0; }
    Material* getMaterial(int x,int y,int z) { return Tile::tiles[getTile(x,y,z)]->material; }
    bool setTileAndData(int,int,int,int,int,int);
    int getHeightmap(int,int);
    int getTopSolidBlock(int,int);
    bool canSeeSky(int x,int y,int z) { return y>=getHeightmap(x,z); }
    int getDaytimeRawBrightness(int,int,int);
    Dimension dimensionStorage;
    Dimension* dimension=&dimensionStorage;
    Writes writes;
    bool inside(int x,int z) const { return x>=-size_/2 && x<size_/2 && z>=-size_/2 && z<size_/2; }
private:
    int64_t seed_;
    int size_;
    std::function<int(int,int)> ground_;
    std::function<uint8_t(int,int,int)> raw_;
    std::map<Position,uint8_t> rawCache_;
    std::map<std::pair<int,int>,int> heights_;
};
class Biome {
public:
    virtual ~Biome() {}
    virtual Feature* getTreeFeature(Random*);
    virtual Feature* getGrassFeature(Random*);
};
class ForestBiome : public Biome { public: Feature* getTreeFeature(Random*) override; };
class TaigaBiome : public Biome { public: Feature* getTreeFeature(Random*) override; };
class JungleBiome : public Biome { public: Feature* getTreeFeature(Random*) override; Feature* getGrassFeature(Random*) override; };
class SwampBiome : public Biome { public: Feature* getTreeFeature(Random*) override; };
class RainforestBiome : public Biome { public: Feature* getTreeFeature(Random*) override; };
struct Counts { int treeCount,flowerCount,grassCount,reedsCount,cactusCount; };
Counts counts(int id);
Biome& biomeFor(int id);
void seedChunk(Level* level,Random* pprandom,int xt,int zt);
void decorateSubset(Level*,Random*,Biome*,Counts,int xo,int zo,bool plants);
std::vector<Block> plan(int64_t seed,int size,int cx,int cz,int biomeID,
                        std::function<int(int,int)> ground,
                        std::function<uint8_t(int,int,int)> raw,bool plants);
} }
