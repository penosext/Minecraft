#include "Platform.h"
#include <array>

namespace original_decoration { namespace detail {
App app;
const int Direction::STEP_X[4]={0,-1,0,1};
const int Direction::STEP_Z[4]={1,0,-1,0};
const int Direction::DIRECTION_OPPOSITE[4]={2,3,0,1};
Tile* Tile::tiles[256]={};
bool Tile::solid[256]={};
int Tile::lightBlock[256]={};
Tile *Tile::reeds=nullptr, *Tile::cactus=nullptr;
namespace {
void initialize() {
    static std::once_flag once;
    std::call_once(once, [] {
        MaterialColor::staticCtor();
        Material::staticCtor();
        // Original Material implementations are compiled, not stand-in booleans.
        // Registry is a physics-only subset of Tile::staticCtor: no game engine.
        static std::vector<std::unique_ptr<Tile>> storage;
        storage.reserve(256);
        for(int i=0;i<256;++i) {
            storage.emplace_back(new Tile(i,Material::stone));
            Tile::tiles[i]=storage.back().get();
            Tile::solid[i]=i!=0;
            Tile::lightBlock[i]=i?255:0;
        }
        auto material=[](std::initializer_list<int> ids,Material* m) {
            for(int i:ids) Tile::tiles[i]->material=m;
        };
        material({0},Material::air);
        material({2},Material::grass);
        material({3,60},Material::dirt);
        material({5,17,25,47,53,54,58,63,64,65,68,72,84,85,96,107,125,126,134,135,136},Material::wood);
        material({8,9},Material::water);
        material({10,11},Material::lava);
        material({12,13,88},Material::sand);
        material({18},Material::leaves);
        material({6,32,37,38,39,40,59,83,104,105,106,111,115,127,141,142},Material::plant);
        material({31},Material::replaceable_plant);
        material({20,102},Material::glass);
        material({30},Material::web);
        material({78},Material::topSnow);
        material({79},Material::ice);
        material({80},Material::snow);
        material({81},Material::cactus);
        material({82},Material::clay);
        material({86,91,103},Material::vegetable);
        material({50,55,66,69,70,75,76,77,93,94,131,132,143},Material::decoration);
        material({51},Material::fire);
        material({90,119},Material::portal);
        for(int i:{0,6,8,9,10,11,18,20,26,27,28,30,31,32,37,38,39,40,44,50,51,53,54,55,
                   59,60,63,64,65,66,67,68,69,70,71,72,75,76,77,78,79,81,83,85,90,92,
                   93,94,95,96,101,102,104,105,106,107,108,109,111,113,114,115,117,118,
                   119,120,126,127,128,130,131,132,134,135,136,139,140,141,142,143,144,
                   145,146,147,148,149,150,151,154,156,157,160,171}) {
            Tile::solid[i]=false;
            Tile::lightBlock[i]=0;
        }
        // Tile::setLightBlock overrides; original terrain fluids/leaves/ice.
        Tile::lightBlock[8]=Tile::lightBlock[9]=Tile::lightBlock[79]=3;
        Tile::lightBlock[10]=Tile::lightBlock[11]=255;
        Tile::lightBlock[18]=Tile::lightBlock[30]=1;
        // Slabs/stairs/farmland are non-solid-render but still light blockers.
        for(int i:{44,53,60,67,108,109,114,126,128,134,135,136,156}) Tile::lightBlock[i]=255;
        for(int i:{31,37,38}) {
            storage.emplace_back(new Bush(i)); Tile::tiles[i]=storage.back().get();
        }
        storage.emplace_back(new ReedTile); Tile::reeds=storage.back().get(); Tile::tiles[83]=Tile::reeds;
        storage.emplace_back(new CactusTile); Tile::cactus=storage.back().get(); Tile::tiles[81]=Tile::cactus;
        // Original trig lookup initialized once, avoiding lazy-init data races.
        Mth::init();
    });
}
}
Level::Level(int64_t seed,int size,std::function<int(int,int)> ground,
             std::function<uint8_t(int,int,int)> raw)
    : seed_(seed),size_(size),ground_(ground),raw_(raw) { initialize(); }
int Level::getTile(int x,int y,int z) {
    if(y<0 || y>=maxBuildHeight || !inside(x,z)) return 0;
    Position p(x,y,z);
    auto w=writes.find(p); if(w!=writes.end()) return w->second.tile;
    auto r=rawCache_.find(p); if(r!=rawCache_.end()) return r->second;
    auto value=raw_(x,y,z); rawCache_.emplace(p,value); return value;
}
bool Level::setTileAndData(int x,int y,int z,int tile,int data,int) {
    if(tile<0 || tile>255 || data<0 || data>15) throw std::out_of_range("original decoration TileID/data");
    if(y<0 || y>=maxBuildHeight || !inside(x,z)) return false;
    writes[Position(x,y,z)]=Block{x,y,z,static_cast<uint8_t>(tile),static_cast<uint8_t>(data)};
    heights_.erase(std::make_pair(x,z));
    return true;
}
int Level::getHeightmap(int x,int z) {
    if(!inside(x,z)) return 0;
    auto key=std::make_pair(x,z);
    auto it=heights_.find(key); if(it!=heights_.end()) return it->second;
    int firstAir=ground_(x,z);
    if(firstAir < -1 || firstAir>256) throw std::out_of_range("ground must return firstAirY -1..256");
    // Heightmap != getTopSolidBlock: water and leaves block skylight. Scan the
    // actual pre-decoration container, including structures above ground.
    int y=maxBuildHeight;
    while(y>0 && Tile::lightBlock[getTile(x,y-1,z)]==0) --y;
    heights_[key]=y; return y;
}
int Level::getTopSolidBlock(int x,int z) {
    if(!inside(x,z)) return -1;
    for(int y=maxBuildHeight-1;y>0;--y) {
        int t=getTile(x,y,z); Material* m=Tile::tiles[t]->material;
        if(t && m->blocksMotion() && m!=Material::leaves) return y+1;
    }
    return -1;
}
int Level::getDaytimeRawBrightness(int x,int y,int z) {
    // Direct sky only; this intentionally does not claim full Level lighting.
    int light=15;
    for(int yy=maxBuildHeight-1;yy>y && light>0;--yy)
        light-=Tile::lightBlock[getTile(x,yy,z)];
    return max(0,light);
}
#include "Counts.inc"
Biome& biomeFor(int id) {
    static Biome base;
    static ForestBiome forest;
    static TaigaBiome taiga;
    static JungleBiome jungle;
    static SwampBiome swamp;
    switch(id) {
    case 4: case 18: return forest;
    case 5: case 19: return taiga;
    case 6: return swamp;
    case 21: case 22: return jungle;
    default: return base;
    }
}
void seedChunk(Level* level,Random* pprandom,int xt,int zt) {
#include "ChunkSeed.inc"
}
void decorateSubset(Level* level,Random* random,Biome* biome,Counts c,int xo,int zo,bool plants) {
    int treeCount=c.treeCount;
#include "Trees.inc"
    if(!plants) return;
    int flowerCount=c.flowerCount,grassCount=c.grassCount,reedsCount=c.reedsCount,cactusCount=c.cactusCount;
    FlowerFeature yellow(Tile::flower_Id),rose(Tile::rose_Id);
    FlowerFeature *yellowFlowerFeature=&yellow,*roseFlowerFeature=&rose;
    ReedsFeature reeds; ReedsFeature* reedsFeature=&reeds;
    CactusFeature cactus; CactusFeature* cactusFeature=&cactus;
#include "FlowersGrass.inc"
#include "Reeds.inc"
#include "Cactus.inc"
}
std::vector<Block> plan(int64_t seed,int size,int cx,int cz,int biomeID,
                        std::function<int(int,int)> ground,
                        std::function<uint8_t(int,int,int)> raw,bool plants) {
    Level level(seed,size,ground,raw);
    Random random(seed);
    seedChunk(&level,&random,cx,cz);
    decorateSubset(&level,&random,&biomeFor(biomeID),counts(biomeID),cx*16,cz*16,plants);
    std::vector<Block> result;
    result.reserve(level.writes.size());
    for(auto& entry:level.writes) result.push_back(entry.second);
    return result;
}
} }
