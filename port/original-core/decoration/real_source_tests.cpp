#include "Decoration.h"
#include <original_biomes/Source.h>
#include "Generator.h"
#include <cassert>
#include <iostream>
#include <map>
#include <set>
#include <tuple>

namespace {
int floorDiv(int x,int d) {return x/d-(x%d<0);}
std::vector<original_terrain::BiomeData> convert(const std::vector<original_biomes::BiomeData>& in) {
    std::vector<original_terrain::BiomeData> out;
    for(auto b:in)out.push_back({b.id,b.depth,b.scale,b.temperature,b.top,b.material});
    return out;
}
struct World {
    static const int size=5120;
    original_biomes::Source source;
    original_terrain::Generator terrain;
    std::map<std::pair<int,int>,std::vector<original_biomes::BiomeData>> biomes;
    std::map<std::pair<int,int>,std::vector<uint8_t>> chunks;
    explicit World(int64_t seed):source(seed),terrain(config(seed),
        [this](int x,int z,int w,int h){return convert(source.raw(x,z,w,h));},
        [this](int x,int z,int w,int h){return convert(source.zoomed(x,z,w,h));}) {}
    static original_terrain::Config config(int64_t seed) {original_terrain::Config c;c.seed=seed;c.worldSize=size;return c;}
    int biome(int x,int z) {
        int cx=floorDiv(x,16),cz=floorDiv(z,16);auto key=std::make_pair(cx,cz);
        auto it=biomes.find(key);
        if(it==biomes.end())it=biomes.emplace(key,source.zoomed(cx*16,cz*16,16,16)).first;
        return it->second[(x-cx*16)+(z-cz*16)*16].id;
    }
    uint8_t tile(int x,int y,int z) {
        assert(x>=-size/2 && x<size/2 && z>=-size/2 && z<size/2);
        if(y<0 || y>=128)return 0;
        int cx=floorDiv(x,16),cz=floorDiv(z,16);auto key=std::make_pair(cx,cz);
        auto it=chunks.find(key);if(it==chunks.end())it=chunks.emplace(key,terrain.generate(cx,cz)).first;
        return it->second[original_terrain::Generator::index(x-cx*16,y,z-cz*16)];
    }
    int ground(int x,int z) {
        for(int y=127;y>0;--y) {
            int t=tile(x,y,z);if(t && !(t>=8 && t<=11) && t!=18 && t!=31 && t!=78)return y+1;
        }
        return -1;
    }
};
bool equal(const std::vector<original_structures::Block>& a,const std::vector<original_structures::Block>& b) {
    if(a.size()!=b.size())return false;
    for(size_t i=0;i<a.size();++i) {
        auto x=a[i],y=b[i];
        if(std::tie(x.x,x.y,x.z,x.tile,x.data)!=std::tie(y.x,y.y,y.z,y.tile,y.data))return false;
    }
    return true;
}
}
int main() {
    size_t totalLogs=0,regions=0;
    std::set<int> actualSpecies,actualBiomes;
    for(int64_t seed:{12345,42}) {
        World world(seed);original_decoration::Options options;options.cachePlans=4;
        original_decoration::Generator gen(seed,World::size,
            [&](int x,int z){return world.biome(x,z);},[&](int x,int z){return world.ground(x,z);},
            [&](int x,int y,int z){return world.tile(x,y,z);},options);
        std::map<int,std::pair<int,int>> found;
        // Genuine Source selection: no synthetic biome passed to decoration.
        auto area=world.source.zoomed(-768,-768,1536,1536);
        for(int z=16;z<1520;z+=16)for(int x=16;x<1520;x+=16) {
            int id=area[x+z*1536].id;
            if(id==4 || id==5 || id==6 || id==18 || id==19 || id==21 || id==22)
                found.emplace(id,std::make_pair(floorDiv(x-768,16)-1,floorDiv(z-768,16)-1));
        }
        assert(!found.empty());
        for(auto entry:found) {
            auto cx=entry.second.first,cz=entry.second.second;
            std::map<std::pair<int,int>,std::vector<original_structures::Block>> forward;
            size_t logs=0;
            for(int x=cx;x<=cx+2;++x)for(int z=cz;z<=cz+2;++z) {
                auto blocks=gen.generateForChunk(x,z);forward[{x,z}]=blocks;
                for(auto b:blocks) {
                    assert(b.x>=x*16 && b.x<x*16+16 && b.z>=z*16 && b.z<z*16+16);
                    assert(b.y>=0 && b.y<256 && b.data<16);
                    if(b.tile==17) {++logs;actualSpecies.insert(b.data&3);}
                }
            }
            for(int x=cx+2;x>=cx;--x)for(int z=cz+2;z>=cz;--z)
                assert(equal(forward[{x,z}],gen.generateForChunk(x,z)));
            totalLogs+=logs;actualBiomes.insert(entry.first);++regions;
            std::cout<<"REAL Source->terrain->decoration seed="<<seed<<" biome="<<entry.first
                     <<" origin="<<cx<<','<<cz<<" logs="<<logs<<" baseChunks="<<world.chunks.size()<<'\n';
        }
    }
    assert(regions>=4 && totalLogs>100 && actualSpecies.count(0) && actualSpecies.count(1) && actualSpecies.count(2));
    std::cout<<"Real bridge passed: regions="<<regions<<" logs="<<totalLogs<<" biomeIDs=";
    for(int id:actualBiomes)std::cout<<id<<',';
    std::cout<<" species=";for(int id:actualSpecies)std::cout<<id<<',';std::cout<<'\n';
}
