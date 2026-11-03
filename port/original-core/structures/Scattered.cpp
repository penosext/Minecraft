#include "Scattered.h"
#include "generated/ScatteredOriginal.h"
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace original_structures {
namespace {
int floorDiv(int n,int d) {return n/d-((n%d)<0);}
int64_t signedBits(uint64_t n) {int64_t r;std::memcpy(&r,&n,sizeof(r));return r;}
int64_t nextLongDefined(Random& r) {
    const int hi=r.nextInt(),lo=r.nextInt();
    return signedBits((uint64_t(int64_t(hi))<<32)+uint64_t(int64_t(lo)));
}
int64_t chunkSeed(int64_t seed,int x,int z) {
    // LargeFeature::apply signed half/addition semantics without signed-shift UB.
    Random r(seed);const uint64_t a=uint64_t(nextLongDefined(r)),b=uint64_t(nextLongDefined(r));
    return signedBits(uint64_t(int64_t(x))*a ^ uint64_t(int64_t(z))*b ^ uint64_t(seed));
}
// Shared original static MossStoneSelector is mutable; serialize across instances.
std::mutex scatteredMutex;
}
struct ScatteredGenerator::Impl {
    struct Plan {
        bool valid=false;
        Landmark landmark{};
        std::map<std::pair<int,int>,std::vector<Block>> chunks;
        uint64_t used=0;
    };
    int64_t seed;int size;
    std::function<int(int,int)> biome,ground;
    std::function<uint8_t(int,int,int)> raw;
    std::map<std::pair<int,int>,Plan> cache;
    uint64_t clock=0;
    Impl(int64_t s,int n,std::function<int(int,int)> b,std::function<int(int,int)> g,
         std::function<uint8_t(int,int,int)> r):seed(s),size(n),biome(b),ground(g),raw(r) {}
    Plan& plan(int rx,int rz) {
        const auto key=std::make_pair(rx,rz);auto old=cache.find(key);
        if(old!=cache.end()) {old->second.used=++clock;return old->second;}
        Plan p;
        scattered_detail::Level level(seed,size,biome,ground,raw);
        // Region candidate is also verified by the extracted original predicate.
        Random* region=level.getRandomFor(rx,rz,14357617);
        int cx=rx*32+region->nextInt(24),cz=rz*32+region->nextInt(24);
        if(cx*16+8>=-size/2 && cx*16+8<size/2 && cz*16+8>=-size/2 && cz*16+8<size/2) {
            Random random(chunkSeed(seed,cx,cz));
            random.nextInt(); // StructureFeature::addFeature clear random key
            scattered_detail::RandomScatteredLargeFeature feature;
            feature.level=&level;feature.random=&random;
            if(feature.isFeatureChunk(cx,cz,false)) {
                scattered_detail::RandomScatteredLargeFeature::ScatteredFeatureStart start(&level,&random,cx,cz);
                auto bb=start.getBoundingBox();int b=biome(cx*16+8,cz*16+8);
                p.valid=true;
                p.landmark=Landmark{signedBits((uint64_t(uint32_t(cx))<<32)|uint32_t(cz)),
                    b==6?"SwamplandHut":(b==21 || b==22?"JunglePyramid":"DesertPyramid"),
                    cx*16+8,cz*16+8,b,bb->x0,bb->z0,bb->x1,bb->z1,static_cast<int>(start.pieces.size())};
                // Canonical full footprint over BASE terrain, exactly as Village.
                // Original postProcess owns all geometry, rotation and height moves.
                detail::BoundingBox clip(-size/2,1,-size/2,size/2-1,255,size/2-1);
                for(auto piece:start.pieces) piece->postProcess(&level,&random,&clip);
                for(auto& item:level.writes) {
                    const auto& block=item.second;
                    p.chunks[{floorDiv(block.x,16),floorDiv(block.z,16)}].push_back(block);
                }
            }
        }
        if(cache.size()>=128) {
            auto oldest=cache.begin();
            for(auto i=cache.begin();i!=cache.end();++i) if(i->second.used<oldest->second.used) oldest=i;
            cache.erase(oldest);
        }
        p.used=++clock;return cache.emplace(key,std::move(p)).first->second;
    }
};
ScatteredGenerator::ScatteredGenerator(int64_t seed,int size,std::function<int(int,int)> biome,
    std::function<int(int,int)> ground,std::function<uint8_t(int,int,int)> raw) {
    if(size<16 || size%16 || size>1048576) throw std::invalid_argument("scattered worldSize must be a multiple of 16 in [16,1048576]");
    if(!biome || !ground || !raw) throw std::invalid_argument("scattered requires pure base callbacks");
    impl_.reset(new Impl(seed,size,biome,ground,raw));
}
ScatteredGenerator::~ScatteredGenerator()=default;
ScatteredGenerator::ScatteredGenerator(ScatteredGenerator&&) noexcept=default;
ScatteredGenerator& ScatteredGenerator::operator=(ScatteredGenerator&&) noexcept=default;
std::vector<Block> ScatteredGenerator::generateForChunk(int cx,int cz) {
    auto& p=*impl_;std::lock_guard<std::mutex> lock(scatteredMutex);
    if(int64_t(cx)*16+15 < -p.size/2 || int64_t(cx)*16>=p.size/2 ||
       int64_t(cz)*16+15 < -p.size/2 || int64_t(cz)*16>=p.size/2) return {};
    std::map<std::tuple<int,int,int>,Block> writes;
    // All original scattered local XZ coordinates are inside width/depth <=21.
    for(int rx=floorDiv(cx*16-20,512);rx<=floorDiv(cx*16+15,512);++rx)
        for(int rz=floorDiv(cz*16-20,512);rz<=floorDiv(cz*16+15,512);++rz) {
            auto& plan=p.plan(rx,rz);auto i=plan.chunks.find({cx,cz});
            if(i!=plan.chunks.end()) for(auto b:i->second) writes[{b.x,b.y,b.z}]=b;
        }
    std::vector<Block> result;result.reserve(writes.size());for(auto& b:writes) result.push_back(b.second);return result;
}
std::vector<Landmark> ScatteredGenerator::nearby(int x,int z,int radius) {
    auto& p=*impl_;std::lock_guard<std::mutex> lock(scatteredMutex);
    if(radius<0 || radius>p.size) throw std::invalid_argument("scattered radius must be 0..worldSize");
    x=std::max(-p.size/2,std::min(p.size/2-1,x));z=std::max(-p.size/2,std::min(p.size/2-1,z));
    int x0=std::max(-p.size/2,x-radius),x1=std::min(p.size/2-1,x+radius);
    int z0=std::max(-p.size/2,z-radius),z1=std::min(p.size/2-1,z+radius);
    std::vector<Landmark> result;
    for(int rx=floorDiv(x0,512);rx<=floorDiv(x1,512);++rx)
        for(int rz=floorDiv(z0,512);rz<=floorDiv(z1,512);++rz) {
            auto& plan=p.plan(rx,rz);auto& l=plan.landmark;
            if(plan.valid && l.sourceX>=x0 && l.sourceX<=x1 && l.sourceZ>=z0 && l.sourceZ<=z1) result.push_back(l);
        }
    return result;
}
std::vector<uint8_t> ScatteredGenerator::emittedTileIDs() {
    return {0,4,5,17,23,24,29,35,44,46,48,54,55,58,67,69,70,85,93,98,106,118,128,131,132,134,140};
}
} // namespace original_structures
