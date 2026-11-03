#include "Structures.h"
#include "Original.h"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <map>
#include <set>
#include <tuple>
#include <stdexcept>
using namespace original_structures;
using Key=std::tuple<int,int,int>;
using Cells=std::map<Key,std::pair<int,int>>;
namespace {
int floorDiv(int x,int d) {return x/d-((x%d)<0);}
std::function<int(int,int)> flat(int h=65) {return [=](int,int){return h;};}
std::function<uint8_t(int,int,int)> base(int h=65) {return [=](int,int y,int){return uint8_t(y==0?7:y<h?1:0);};}
Generator generator(int64_t seed=12345,int b=1,int h=65,Options o=Options()) {
    return Generator(seed,4096,[=](int,int){return b;},flat(h),base(h),o);
}
Cells cells(const std::vector<Block>& blocks) {
    Cells r;for(const auto& b:blocks) {assert(b.data<=15);assert(!r.count({b.x,b.y,b.z}));r[{b.x,b.y,b.z}]={b.tile,b.data};}return r;
}
uint64_t hash(const std::vector<Block>& blocks) {
    uint64_t h=1469598103934665603ULL;
    for(const auto& b:blocks) for(int n:{b.x,b.y,b.z,int(b.tile),int(b.data)}) {h^=uint32_t(n);h*=1099511628211ULL;}return h;
}
Landmark first(Generator& g) {auto all=g.nearby(0,0,900);assert(!all.empty());return all.front();}
Cells collect(Generator& g,const Landmark& l,bool reverse=false) {
    std::vector<std::pair<int,int>> chunks;
    for(int x=floorDiv(l.minX,16);x<=floorDiv(l.maxX,16);++x)
        for(int z=floorDiv(l.minZ,16);z<=floorDiv(l.maxZ,16);++z) chunks.push_back({x,z});
    if(reverse) std::reverse(chunks.begin(),chunks.end());
    Cells out;
    for(auto c:chunks) for(const auto& b:g.generateForChunk(c.first,c.second)) {
        assert(b.x>=c.first*16 && b.x<c.first*16+16 && b.z>=c.second*16 && b.z<c.second*16+16);
        out[{b.x,b.y,b.z}]={b.tile,b.data};
    }
    return out;
}
}
int main(int argc,char** argv) {
    assert(argc==2);std::string test=argv[1];
    auto g=generator();auto l=first(g);int cx=floorDiv(l.sourceX,16),cz=floorDiv(l.sourceZ,16);
    if(test=="deterministic" || test=="retry") {
        auto a=g.generateForChunk(cx,cz);auto b=g.generateForChunk(cx,cz);auto other=generator();
        assert(!a.empty());assert(cells(a)==cells(b));assert(cells(a)==cells(other.generateForChunk(cx,cz)));
        std::cout<<"seed=12345 flat-fixture start="<<l.sourceX<<','<<l.sourceZ<<" hash="<<hash(a)<<'\n';
    } else if(test=="seed") {
        auto different=generator(-987654321);auto all=different.nearby(0,0,900);assert(!all.empty());
        assert(all.front().id!=l.id || cells(different.generateForChunk(cx,cz))!=cells(g.generateForChunk(cx,cz)));
        // Independent reference for original regional seed, negative region flooring,
        // spacing/min separation and TWO sequential original Random nextInt draws.
        for(auto lm:g.nearby(0,0,1600)) {
            int x=floorDiv(lm.sourceX,16),z=floorDiv(lm.sourceZ,16),rx=floorDiv(x,32),rz=floorDiv(z,32);
            Random r(int64_t(rx)*341873128712LL+int64_t(rz)*132897987541LL+12345+10387312);
            assert(x==rx*32+r.nextInt(24));assert(z==rz*32+r.nextInt(24));
        }
    } else if(test=="biome") {
        for(int b:{0,3,4,6,17,21}) {auto bad=generator(12345,b);assert(bad.nearby(0,0,1000).empty());}
        auto desert=generator(12345,2);auto dl=first(desert);auto output=collect(desert,dl);bool sandstone=false,smooth=false;
        for(auto b:output) {sandstone|=b.second.first==24;smooth|=b.second.first==24 && b.second.second==2;}
        assert(sandstone && smooth);
        Options o;o.generationBiome=[](int,int){return 4;};auto disallowed=generator(12345,1,65,o);
        assert(disallowed.nearby(0,0,900).empty());
    } else if(test=="seams") {
        auto second=generator();assert(collect(g,l)==collect(second,l,true));
        // Compare chunk clipping against DIRECT upstream VillageStart/postProcess
        // with one whole-world box. No hand-authored template oracle.
        namespace d=original_structures::detail;
        d::Level level(12345,4096,[](int,int){return 1;},flat(),base(),{});
        Random root(12345);auto nextLong=[&](){int hi=root.nextInt(),lo=root.nextInt();return int64_t((uint64_t(int64_t(hi))<<32)+uint64_t(int64_t(lo)));};
        uint64_t sx=nextLong(),sz=nextLong();Random r(int64_t(uint64_t(int64_t(cx))*sx ^ uint64_t(int64_t(cz))*sz ^ uint64_t(12345)));r.nextInt();
        d::VillageFeature::VillageStart start(&level,&r,cx,cz,0,4096);assert(start.isValid());
        d::BoundingBox bb(-2048,1,-2048,2047,255,2047);for(auto piece:start.pieces) piece->postProcess(&level,&r,&bb);
        auto actual=collect(g,l);assert(!level.writes.empty());
        for(auto& p:level.writes) assert(actual.at(p.first)==std::make_pair(int(p.second.tile),int(p.second.data)));
    } else if(test=="regions") {
        Options o;o.cacheRegions=2;auto tiny=generator(12345,1,65,o);auto landmarks=tiny.nearby(0,0,1700);assert(landmarks.size()>10);
        auto initial=tiny.generateForChunk(cx,cz);for(auto lm:landmarks) tiny.generateForChunk(floorDiv(lm.sourceX,16),floorDiv(lm.sourceZ,16));
        assert(cells(initial)==cells(tiny.generateForChunk(cx,cz)));
    } else if(test=="ground") {
        auto high=generator(12345,1,85);auto lowCells=collect(g,l);auto highCells=collect(high,l);
        assert(lowCells.size()==highCells.size());
        for(auto p:lowCells) assert(highCells.at({std::get<0>(p.first),std::get<1>(p.first)+20,std::get<2>(p.first)})==p.second);
        auto slope=[](int x,int z){return 68+((x%7+7)%7)+((z%5+5)%5);};
        auto raw=[=](int x,int y,int z){return uint8_t(y<slope(x,z)?1:0);};
        Generator a(12345,4096,[](int,int){return 1;},slope,raw),b(12345,4096,[](int,int){return 1;},slope,raw);
        assert(collect(a,l)==collect(b,l,true));
    } else if(test=="metadata") {
        auto output=collect(g,l);std::set<int> doors,stairs,crops;bool wool15=false;
        auto declared=Generator::emittedTileIDs();
        for(auto p:output) {
            int tile=p.second.first,data=p.second.second;assert(std::find(declared.begin(),declared.end(),tile)!=declared.end());
            if(tile==64) doors.insert(data);if(tile==53 || tile==67) stairs.insert(data);
            if(tile==59 || tile==141 || tile==142) {assert(data>=2 && data<=7);crops.insert(data);}
            if(tile==35) {assert(data==15);wool15=true;}
        }
        assert(!doors.empty() && *doors.rbegin()>=8);assert(stairs.size()>1);assert(crops.size()>1);
        auto unsupported=Generator::unsupportedIDs([](uint8_t id){return id<128;});assert(unsupported==std::vector<uint8_t>({128,141,142}));
        (void)wool15; // This seed need not select fallback LightPost; templates covers it.
    } else if(test=="bounds") {
        assert(g.generateForChunk(INT_MAX,INT_MIN).empty());assert(g.generateForChunk(-129,0).empty());
        for(auto lm:g.nearby(0,0,4096)) {assert(lm.minX>=-2048 && lm.minZ>=-2048 && lm.maxX<2048 && lm.maxZ<2048);}
        bool rejected=false;try {Generator invalid(0,17,{}, {}, {});} catch(const std::invalid_argument&) {rejected=true;}assert(rejected);
    } else if(test=="templates") {
        namespace d=original_structures::detail;
        std::set<std::string> seen;bool lightWool15=false;
        for(int seed=0;seed<40;++seed) {
            d::Level level(seed,4096,[](int,int){return 1;},flat(),base(),{});Random r(seed);
            d::VillageFeature::VillageStart start(&level,&r,0,0,0,4096);
            for(auto piece:start.pieces) {
#define CHECK(name) if(dynamic_cast<d::VillagePieces::name*>(piece)) seen.insert(#name)
                CHECK(Well);CHECK(StraightRoad);CHECK(SimpleHouse);CHECK(SmallTemple);CHECK(BookHouse);CHECK(SmallHut);
                CHECK(PigHouse);CHECK(TwoRoomHouse);CHECK(Smithy);CHECK(Farmland);CHECK(DoubleFarmland);CHECK(LightPost);
#undef CHECK
                d::BoundingBox box(-2048,1,-2048,2047,255,2047);assert(piece->postProcess(&level,&r,&box));
            }
            for(auto p:level.writes) if(p.second.tile==35) {assert(p.second.data==15);lightWool15=true;}
        }
        for(auto n:seen) std::cout<<n<<' ';std::cout<<'\n';assert(seen.size()==12);assert(lightWool15);
    } else return 2;
    std::cout<<test<<" passed\n";
}
