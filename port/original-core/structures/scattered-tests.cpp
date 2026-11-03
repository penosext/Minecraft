#include "Scattered.h"
#include "generated/ScatteredOriginal.h"
#include <cassert>
#include <climits>
#include <iostream>
#include <set>
#ifdef SCATTERED_REAL_WORLD_TESTS
#include <original_biomes/Source.h>
#include "Generator.h"
#endif

namespace {
using original_structures::ScatteredGenerator;
using original_structures::Block;
using original_structures::Landmark;
int divFloor(int x,int d) {return x/d-((x%d)<0);}
using Writes=std::map<std::tuple<int,int,int>,std::pair<int,int>>;
Writes normalized(const std::vector<Block>& blocks) {
    Writes w;for(auto b:blocks) {assert(!w.count({b.x,b.y,b.z}));w[{b.x,b.y,b.z}]={b.tile,b.data};}return w;
}
int flatTop(int,int) {return 64;}
uint8_t flatTile(int,int y,int) {return y<64?1:0;}
void checkBounds(const std::vector<Block>& blocks,int cx,int cz,int size) {
    auto ids=ScatteredGenerator::emittedTileIDs();std::set<int> allowed(ids.begin(),ids.end());
    std::tuple<int,int,int> previous{INT_MIN,INT_MIN,INT_MIN};
    for(auto b:blocks) {
        auto key=std::make_tuple(b.x,b.y,b.z);assert(previous<key);previous=key;
        assert(divFloor(b.x,16)==cx && divFloor(b.z,16)==cz);
        assert(b.x>=-size/2 && b.x<size/2 && b.z>=-size/2 && b.z<size/2);
        assert(b.y>=1 && b.y<=255 && b.data<=15 && allowed.count(b.tile));
    }
}
std::vector<Block> whole(ScatteredGenerator& g,const Landmark& l,int size,bool reverse=false) {
    std::vector<std::pair<int,int>> chunks;
    for(int x=divFloor(l.minX,16);x<=divFloor(l.maxX,16);++x)
        for(int z=divFloor(l.minZ,16);z<=divFloor(l.maxZ,16);++z) chunks.push_back({x,z});
    if(reverse) std::reverse(chunks.begin(),chunks.end());
    std::vector<Block> out;
    for(auto c:chunks) {
        auto b=g.generateForChunk(c.first,c.second);checkBounds(b,c.first,c.second,size);
        out.insert(out.end(),b.begin(),b.end());
    }
    return out;
}
void synthetic() {
    const int size=2048;int cases=0;
    for(int biome:{2,17,21,22,6}) {
        auto bi=[=](int,int){return biome;};
        ScatteredGenerator a(12345,size,bi,flatTop,flatTile),b(12345,size,bi,flatTop,flatTile);
        auto ls=a.nearby(0,0,size);assert(ls.size()>=9);
        std::set<int> orientations;
        for(auto l:ls) {
            assert(l.biome==biome && l.pieceCount==1);
            assert(l.type==(biome==6?"SwamplandHut":(biome>=21?"JunglePyramid":"DesertPyramid")));
            assert(l.sourceX==l.minX+8 && l.sourceZ==l.minZ+8);
            const int w=l.maxX-l.minX+1,d=l.maxZ-l.minZ+1;
            assert((biome==2 || biome==17)?w==21 && d==21:(biome==6?(w==7 && d==9)||(w==9 && d==7):(w==12 && d==15)||(w==15 && d==12)));
            auto only=a.nearby(l.sourceX,l.sourceZ,0);assert(only.size()==1 && only[0].id==l.id);
            auto wa=whole(a,l,size),wb=whole(b,l,size,true);assert(!wa.empty());assert(normalized(wa)==normalized(wb));
            assert(normalized(wa)==normalized(whole(a,l,size)));
            std::set<int> ids;for(auto block:wa) ids.insert(block.tile);
            assert(ids.count(0));
            if(biome==2 || biome==17) {assert(ids.count(24) && ids.count(35) && ids.count(46) && ids.count(54));}
            if(biome==21 || biome==22) {assert(ids.count(4) && ids.count(48) && ids.count(23) && ids.count(54) && ids.count(29));}
            if(biome==6) {assert(ids.count(5) && ids.count(17) && ids.count(118) && ids.count(140));}
            ++cases;
        }
        assert(a.generateForChunk(INT_MIN,INT_MAX).empty());
        // More than 128 plans forces eviction; regenerated whole-piece is identical.
        ScatteredGenerator big(42,8192,bi,flatTop,flatTile);
        auto first=big.nearby(-3500,-3500,700);assert(!first.empty());
        auto before=normalized(whole(big,first.front(),8192));
        big.nearby(0,0,8192);assert(before==normalized(whole(big,first.front(),8192)));
    }
    // Disallowed biome IDs never receive a substitute temple.
    for(int id:{0,1,3,4,5,7,16,20,23}) {
        ScatteredGenerator g(42,512,[=](int,int){return id;},flatTop,flatTile);
        assert(g.nearby(0,0,512).empty());assert(g.generateForChunk(-1,-1).empty());
    }
    // Exact original predicate against an independent region-seed oracle, negatives included.
    namespace sd=original_structures::scattered_detail;
    sd::Level level(42,4096,[](int,int){return 21;},flatTop,flatTile);
    sd::RandomScatteredLargeFeature feature;Random unused(42);feature.level=&level;feature.random=&unused;
    for(int rx=-3;rx<=3;++rx) for(int rz=-3;rz<=3;++rz) {
        Random region(int64_t(rx)*341873128712LL+int64_t(rz)*132897987541LL+42+14357617);
        int cx=rx*32+region.nextInt(24),cz=rz*32+region.nextInt(24);
        assert(feature.isFeatureChunk(cx,cz));assert(!feature.isFeatureChunk(cx+1,cz));
    }
    // Original request-only Witch factory executes all retained spawn call sites.
    Random r(9);sd::ScatteredFeaturePieces::SwamplandHut hut(&r,-16,-16);
    sd::BoundingBox clip(-128,1,-128,127,255,127);
    assert(hut.postProcess(&level,&r,&clip));assert(level.spawnRequests.size()==1);
    assert(level.spawnRequests[0].finalized);
    assert(hut.postProcess(&level,&r,&clip));assert(level.spawnRequests.size()==1);
    // Odd number-of-chunks finite worlds, including partial negative edge chunk.
    for(int seed=0;seed<40;++seed) {
        ScatteredGenerator edge(seed,528,[](int,int){return 2;},flatTop,flatTile);
        for(auto l:edge.nearby(0,0,528)) whole(edge,l,528);
        assert(edge.generateForChunk(17,0).empty());assert(edge.generateForChunk(-18,0).empty());
    }
    std::cout<<"Scattered synthetic biome/geometry/retry/seam/filter/negative/bounds/cache/spawn passed, pieces="<<cases<<'\n';
}
#ifdef SCATTERED_REAL_WORLD_TESTS
std::vector<original_terrain::BiomeData> convert(const std::vector<original_biomes::BiomeData>& input) {
    std::vector<original_terrain::BiomeData> out;
    for(auto b:input) out.push_back({b.id,b.depth,b.scale,b.temperature,b.top,b.material});return out;
}
struct RealWorld {
    original_biomes::Source source;
    original_terrain::Generator terrain;
    std::map<std::pair<int,int>,std::vector<original_biomes::BiomeData>> biomes;
    std::map<std::pair<int,int>,std::vector<uint8_t>> chunks;
    explicit RealWorld(int64_t seed):source(seed),terrain(config(seed),
        [this](int x,int z,int w,int h){return convert(source.raw(x,z,w,h));},
        [this](int x,int z,int w,int h){return convert(source.zoomed(x,z,w,h));}) {}
    static original_terrain::Config config(int64_t seed) {original_terrain::Config c;c.seed=seed;c.worldSize=5120;return c;}
    int biome(int x,int z) {
        int cx=divFloor(x,16),cz=divFloor(z,16);auto key=std::make_pair(cx,cz);
        auto it=biomes.find(key);if(it==biomes.end()) it=biomes.emplace(key,source.zoomed(cx*16,cz*16,16,16)).first;
        return it->second[x-cx*16+(z-cz*16)*16].id;
    }
    uint8_t tile(int x,int y,int z) {
        if(y<0 || y>=128) return 0;int cx=divFloor(x,16),cz=divFloor(z,16);auto key=std::make_pair(cx,cz);
        auto it=chunks.find(key);if(it==chunks.end()) it=chunks.emplace(key,terrain.generate(cx,cz)).first;
        return it->second[original_terrain::Generator::index(x-cx*16,y,z-cz*16)];
    }
    int top(int x,int z) {
        for(int y=127;y>0;--y) {int t=tile(x,y,z);if(t && !(t>=8 && t<=11) && t!=18 && t!=31 && t!=78) return y+1;}return -1;
    }
};
void realWorld() {
    std::set<std::string> types;size_t count=0;
    for(int64_t seed:{12345,0,42}) {
        RealWorld w(seed);
        ScatteredGenerator g(seed,5120,[&](int x,int z){return w.biome(x,z);},
            [&](int x,int z){return w.top(x,z);},[&](int x,int y,int z){return w.tile(x,y,z);});
        auto landmarks=g.nearby(0,0,2500);
        for(auto l:landmarks) {
            assert(l.biome==w.biome(l.sourceX,l.sourceZ));
            auto blocks=whole(g,l,5120);assert(!blocks.empty());
            auto retry=whole(g,l,5120,true);assert(normalized(blocks)==normalized(retry));
            types.insert(l.type);++count;
            std::cout<<"REAL scattered seed="<<seed<<" type="<<l.type<<" source="<<l.sourceX<<','<<l.sourceZ<<" biome="<<l.biome<<" bbox="<<l.minX<<','<<l.minZ<<','<<l.maxX<<','<<l.maxZ<<" writes="<<blocks.size()<<'\n';
        }
    }
    assert(types.size()==3);assert(count>=3);
    std::cout<<"Real original-biome/terrain scattered callback chain passed, landmarks="<<count<<" types="<<types.size()<<'\n';
}
#endif
}
int main(int argc,char** argv) {
    std::string mode=argc>1?argv[1]:"all";
    if(mode=="all" || mode=="synthetic") synthetic();
#ifdef SCATTERED_REAL_WORLD_TESTS
    if(mode=="all" || mode=="real") realWorld();
#endif
}
