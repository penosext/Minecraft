#include "Original.h"
#include <mutex>
#include <stdexcept>

namespace original_structures { namespace detail {
const int Direction::DIRECTION_OPPOSITE[4]={2,3,0,1};
const int Facing::OPPOSITE_FACING[6]={1,0,3,2,5,4};
namespace {
Biome biomeTableStorage[3]={Biome(1),Biome(2),Biome(17)};
Biome otherBiome(-1);
Tile railStorage(66),ladderStorage(65),buttonStorage(77),doorStorage(64);
Material solidMaterial{false},liquidMaterial{true};
}
Biome* Biome::plains=&biomeTableStorage[0];
Biome* Biome::desert=&biomeTableStorage[1];
Biome* Biome::desertHills=&biomeTableStorage[2];
Tile *Tile::rail=&railStorage,*Tile::ladder=&ladderStorage,*Tile::button=&buttonStorage,*Tile::door_wood=&doorStorage;
Tile* Tile::tiles[256]={};
BiomeSource::BiomeSource(std::function<int(int,int)> full,std::function<int(int,int)> generation):full_(full),generation_(generation) {}
Biome* BiomeSource::getBiome(int x,int z) {
    int id=full_(x,z);
    for(auto& b:biomeTableStorage) if(b.id==id) return &b;
    return &otherBiome;
}
bool BiomeSource::containsOnly(int x,int z,int r,vector<Biome*> allowed) {
    // Exact upstream sampling lattice; generation callback is the pre-Voronoi Layer.
    for(int qz=(z-r)>>2;qz<=((z+r)>>2);++qz)
        for(int qx=(x-r)>>2;qx<=((x+r)>>2);++qx) {
            int id=generation_ ? generation_(qx,qz) : full_(qx*4,qz*4);
            bool ok=false;
            for(auto b:allowed) if(b->id==id) {ok=true;break;}
            if(!ok) return false;
        }
    return true;
}
Level::Level(int64_t seed,int size,std::function<int(int,int)> biome,std::function<int(int,int)> ground,
             std::function<uint8_t(int,int,int)> raw,std::function<int(int,int)> generation):
    size_(size),ground_(ground),raw_(raw),dimensionStorage_{size/16},data_{size/16,seed},
    biomes_(biome,generation),randomStorage_(seed),dimension(&dimensionStorage_),random(&randomStorage_) {
    static std::once_flag flag;
    std::call_once(flag,[]{VillageFeature::staticCtor();});
}
int Level::getTopSolidBlock(int x,int z) {
    // Deliberately BASE-only even when writes exist: no structure feedback/recursion.
    if(x < -size_/2 || x>=size_/2 || z < -size_/2 || z>=size_/2) return -1;
    int y=ground_(x,z);
    if(y < -1 || y>256) throw std::out_of_range("structures groundHeight must be -1..256 (solid Y+1)");
    return y;
}
int Level::getTile(int x,int y,int z) {
    if(y<0 || y>=256 || x < -size_/2 || x>=size_/2 || z < -size_/2 || z>=size_/2) return 0;
    auto it=writes.find(std::make_tuple(x,y,z));
    return it==writes.end()?raw_(x,y,z):it->second.tile;
}
bool Level::setTileAndData(int x,int y,int z,int tile,int data,int) {
    if(tile<0 || tile>255 || data<0 || data>15) throw std::out_of_range("original structure TileID/data out of range");
    if(y<1 || y>=256 || x < -size_/2 || x>=size_/2 || z < -size_/2 || z>=size_/2) return false;
    writes[std::make_tuple(x,y,z)]=Block{x,y,z,static_cast<uint8_t>(tile),static_cast<uint8_t>(data)};
    return true;
}
Material* Level::getMaterial(int x,int y,int z) {
    int t=getTile(x,y,z); return t>=8 && t<=11?&liquidMaterial:&solidMaterial;
}
bool Level::isSolidBlockingTile(int x,int y,int z) {
    // Door hinge solidRender semantics for all village-emitted IDs. Unknown BASE
    // IDs use conservative opaque treatment; full Tile/material physics is absent.
    switch(getTile(x,y,z)) {
    case 0:case 6:case 8:case 9:case 10:case 11:case 18:case 20:case 26:
    case 27:case 28:case 30:case 31:case 32:case 37:case 38:case 39:case 40:
    case 44:case 50:case 51:case 53:case 54:case 55:case 59:case 60:case 63:
    case 64:case 65:case 66:case 67:case 68:case 69:case 70:case 71:case 72:
    case 75:case 76:case 77:case 78:case 79:case 81:case 83:case 85:case 90:
    case 92:case 93:case 94:case 95:case 96:case 101:case 102:case 104:case 105:
    case 106:case 107:case 108:case 109:case 111:case 113:case 114:case 115:
    case 117:case 118:case 119:case 120:case 126:case 127:case 128:case 130:
    case 131:case 132:case 134:case 135:case 136:case 139:case 140:case 141:case 142:
    case 143:case 144:case 145:case 146:case 147:case 148:case 149:case 150:
    case 151:case 154:case 156:case 157:case 160:case 171: return false;
    default: return true;
    }
}
}}
