#include "Structures.h"
#include <original_biomes/Source.h>
#include "Generator.h"
#include <cassert>
#include <iostream>
#include <map>
#include <memory>
#include <set>

namespace {
int divFloor(int x,int d){return x/d-((x%d)<0);}
std::vector<original_terrain::BiomeData> convert(const std::vector<original_biomes::BiomeData>& input) {
    std::vector<original_terrain::BiomeData> out;
    for(auto b:input) out.push_back({b.id,b.depth,b.scale,b.temperature,b.top,b.material});return out;
}
struct RealWorld {
    original_biomes::Source source;
    original_terrain::Generator terrain;
    std::map<std::pair<int,int>,std::vector<original_biomes::BiomeData>> full,quarter;
    std::map<std::pair<int,int>,std::vector<uint8_t>> chunks;
    explicit RealWorld(int64_t seed):source(seed),terrain(config(seed),
        [this](int x,int z,int w,int h){return convert(source.raw(x,z,w,h));},
        [this](int x,int z,int w,int h){return convert(source.zoomed(x,z,w,h));}) {}
    static original_terrain::Config config(int64_t s) {original_terrain::Config c;c.seed=s;c.worldSize=5120;return c;}
    int biome(int x,int z,bool raw) {
        int cx=divFloor(x,16),cz=divFloor(z,16);auto& cache=raw?quarter:full;auto key=std::make_pair(cx,cz);
        auto it=cache.find(key);
        if(it==cache.end()) it=cache.emplace(key,raw?source.raw(cx*16,cz*16,16,16):source.zoomed(cx*16,cz*16,16,16)).first;
        return it->second[(x-cx*16)+(z-cz*16)*16].id;
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
}
int main() {
    size_t total=0;
    for(int64_t seed:{12345,0,42}) {
        RealWorld world(seed);original_structures::Options options;
        options.generationBiome=[&](int x,int z){return world.biome(x,z,true);};
        original_structures::Generator structures(seed,5120,[&](int x,int z){return world.biome(x,z,false);},
            [&](int x,int z){return world.top(x,z);},[&](int x,int y,int z){return world.tile(x,y,z);},options);
        auto landmarks=structures.nearby(0,0,2300);assert(!landmarks.empty());
        for(auto l:landmarks) {
            assert(l.biome==1 || l.biome==2 || l.biome==17);
            int cx=divFloor(l.sourceX,16),cz=divFloor(l.sourceZ,16);
            auto first=structures.generateForChunk(cx,cz),again=structures.generateForChunk(cx,cz);
            assert(!first.empty());assert(first.size()==again.size());
            for(size_t i=0;i<first.size();++i) assert(first[i].x==again[i].x && first[i].y==again[i].y && first[i].z==again[i].z && first[i].tile==again[i].tile && first[i].data==again[i].data);
            std::set<int> ids;for(auto b:first) ids.insert(b.tile);
            std::cout<<"REAL seed="<<seed<<" sourceStart="<<l.sourceX<<','<<l.sourceZ
                <<" sourceChunk="<<cx<<','<<cz<<" biome="<<l.biome<<" pieces="<<l.pieceCount
                <<" firstChunkWrites="<<first.size()<<" worldStart="<<l.sourceX+2560<<','<<l.sourceZ+2560<<" ids=";
            for(int id:ids) std::cout<<id<<',';std::cout<<'\n';
            ++total;
        }
    }
    assert(total>=3);std::cout<<"Real biome/terrain/village callback chain passed, villages="<<total<<'\n';
}
