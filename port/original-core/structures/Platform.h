#pragma once
#include "Structures.h"
#include <cstdint>
using byte = unsigned char;
#include "Random.h"
#include <algorithm>
#include <cassert>
#include <climits>
#include <cstdlib>
#include <list>
#include <map>
#include <tuple>
#include <vector>
#include <string>

namespace original_structures { namespace detail {
using std::list; using std::vector; using std::wstring; using std::find;
struct Math {
    template<class T> static T _max(T a,T b) {return std::max(a,b);}
    template<class T> static T _min(T a,T b) {return std::min(a,b);}
};
struct Mth { static int nextInt(Random*,int,int); };
struct Direction {
    static const int UNDEFINED=-1,SOUTH=0,WEST=1,NORTH=2,EAST=3;
    static const int DIRECTION_OPPOSITE[4];
};
struct Facing {
    static const int DOWN=0,UP=1,NORTH=2,SOUTH=3,WEST=4,EAST=5;
    static const int OPPOSITE_FACING[6];
};
struct SandStoneTile { static const int TYPE_DEFAULT=0,TYPE_SMOOTHSIDE=2; };
struct BaseRailTile { static const int DIR_FLAT_Z=0,DIR_FLAT_X=1; };
struct DyePowderItem { static const int WHITE=15; };
struct Biome {
    int id; explicit Biome(int n):id(n) {}
    static Biome* plains; static Biome* desert; static Biome* desertHills;
};
class BiomeSource {
    std::function<int(int,int)> full_, generation_;
public:
    BiomeSource(std::function<int(int,int)> full,std::function<int(int,int)> generation);
    Biome* getBiome(int x,int z);
    bool containsOnly(int x,int z,int r,vector<Biome*> allowed);
};
struct Dimension {
    int chunks;
    int getXZSize() {return chunks;}
    int getSpawnYPosition() {return 64;}
};
struct LevelData {
    int chunks; int64_t seed;
    int getXZSize() {return chunks;}
    int64_t getSeed() {return seed;}
};
struct Material {
    bool liquid;
    bool isLiquid() {return liquid;}
};
class Level {
    int size_;
    std::function<int(int,int)> ground_;
    std::function<uint8_t(int,int,int)> raw_;
    Dimension dimensionStorage_;
    LevelData data_;
    BiomeSource biomes_;
    Random randomStorage_;
public:
    static const int maxBuildHeight=256;
    Dimension* dimension;
    Random* random;
    std::map<std::tuple<int,int,int>,Block> writes;
    Level(int64_t seed,int size,std::function<int(int,int)> biome,
          std::function<int(int,int)> ground,std::function<uint8_t(int,int,int)> raw,
          std::function<int(int,int)> generation);
    LevelData* getLevelData() {return &data_;}
    int64_t getSeed() {return data_.seed;}
    BiomeSource* getBiomeSource() {return &biomes_;}
    Random* getRandomFor(int,int,int);
    int getTopSolidBlock(int x,int z);
    int getTile(int x,int y,int z);
    bool isEmptyTile(int x,int y,int z) {return getTile(x,y,z)==0;}
    bool isSolidBlockingTile(int x,int y,int z);
    Material* getMaterial(int x,int y,int z);
    bool setTileAndData(int x,int y,int z,int tile,int data,int flags);
    // No tile neighbor simulation. Door metadata is emitted explicitly.
    void updateNeighborsAt(int,int,int,int) {}
};
struct Tile;
struct DoorItem { static void place(Level*,int,int,int,int,Tile*); };
}} // namespace original_structures::detail
