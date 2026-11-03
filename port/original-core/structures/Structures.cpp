#include "Structures.h"
#include "Scattered.h"
#include "Original.h"
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace original_structures {
namespace {
int floorDiv(int n,int d) {int q=n/d; return q-((n%d)<0);}
int64_t signedBits(uint64_t n) {int64_t r; std::memcpy(&r,&n,sizeof(r)); return r;}
int64_t nextLongDefined(Random& r) {
    // Same signed halves/addition as upstream Random::nextLong, without its
    // negative signed-left-shift UB. Random itself is linked from original_core.
    const int hi=r.nextInt(),lo=r.nextInt();
    return signedBits((uint64_t(int64_t(hi))<<32)+uint64_t(int64_t(lo)));
}
int64_t chunkSeed(int64_t seed,int x,int z) {
    Random r(seed); uint64_t a=uint64_t(nextLongDefined(r)),b=uint64_t(nextLongDefined(r));
    return signedBits(uint64_t(int64_t(x))*a ^ uint64_t(int64_t(z))*b ^ uint64_t(seed));
}
}
struct Generator::Impl {
    struct Plan {
        bool valid=false;
        Landmark landmark{};
        std::map<std::pair<int,int>,std::vector<Block>> chunks;
        uint64_t used=0;
    };
    int64_t seed; int size,spacing;
    std::function<int(int,int)> biome,ground;
    std::function<uint8_t(int,int,int)> raw;
    Options options;
    ScatteredGenerator scattered;
    std::map<std::pair<int,int>,Plan> cache;
    uint64_t clock=0;
    std::mutex mutex;
    Impl(int64_t s,int n,std::function<int(int,int)> b,std::function<int(int,int)> g,
         std::function<uint8_t(int,int,int)> r,Options o):seed(s),size(n),spacing(n/16<128?16:32),biome(b),ground(g),raw(r),options(o),scattered(s,n,b,g,r) {}
    Plan& plan(int rx,int rz) {
        const auto key=std::make_pair(rx,rz);
        auto old=cache.find(key);
        if(old!=cache.end()) {old->second.used=++clock;return old->second;}
        Plan result;
        // Exact VillageFeature region candidate seed and draws.
        Random region(signedBits(uint64_t(int64_t(rx))*341873128712ULL+
                                 uint64_t(int64_t(rz))*132897987541ULL+uint64_t(seed)+10387312ULL));
        int cx=rx*spacing+region.nextInt(spacing-8),cz=rz*spacing+region.nextInt(spacing-8);
        if(cx*16+8>=-size/2 && cx*16+8<size/2 && cz*16+8>=-size/2 && cz*16+8<size/2) {
            detail::Level level(seed,size,biome,ground,raw,options.generationBiome);
            Random random(chunkSeed(seed,cx,cz));
            random.nextInt(); // StructureFeature::addFeature clear random key
            detail::VillageFeature feature(size);
            feature.level=&level; feature.random=&random;
            if(feature.isFeatureChunk(cx,cz,false)) {
                // FULL original VillageStart roads-first randomized queues, weights,
                // collision/depth/biome checks, and all nine weighted house classes.
                detail::VillageFeature::VillageStart start(&level,&random,cx,cz,0,size);
                if(start.isValid()) {
                    result.valid=true;
                    auto bb=start.getBoundingBox();
                    result.landmark=Landmark{signedBits((uint64_t(uint32_t(cx))<<32)|uint32_t(cz)),
                        "Village",cx*16+2,cz*16+2,biome(cx*16+2,cz*16+2),
                        bb->x0,bb->z0,bb->x1,bb->z1,static_cast<int>(start.pieces.size())};
                    // One canonical whole-plan postProcess. This deliberately differs
                    // from upstream's first-generated-chunk height slice and decoration
                    // RNG. BASE heights plus full footprint ensure no order dependence.
                    detail::BoundingBox clip(-size/2,1,-size/2,size/2-1,255,size/2-1);
                    for(auto piece:start.pieces) piece->postProcess(&level,&random,&clip);
                    for(auto& item:level.writes) {
                        const Block& b=item.second;
                        result.chunks[{floorDiv(b.x,16),floorDiv(b.z,16)}].push_back(b);
                        result.landmark.minX=std::min(result.landmark.minX,b.x);
                        result.landmark.minZ=std::min(result.landmark.minZ,b.z);
                        result.landmark.maxX=std::max(result.landmark.maxX,b.x);
                        result.landmark.maxZ=std::max(result.landmark.maxZ,b.z);
                    }
                }
            }
        }
        if(cache.size()>=options.cacheRegions) {
            auto oldest=cache.begin();
            for(auto it=cache.begin();it!=cache.end();++it) if(it->second.used<oldest->second.used) oldest=it;
            cache.erase(oldest);
        }
        result.used=++clock;
        return cache.emplace(key,std::move(result)).first->second;
    }
};
Generator::Generator(int64_t seed,int size,std::function<int(int,int)> biome,
    std::function<int(int,int)> ground,std::function<uint8_t(int,int,int)> raw,Options options) {
    if(size<16 || size%16 || size>1048576) throw std::invalid_argument("structures worldSize must be a multiple of 16 in [16,1048576]");
    if(!biome || !ground || !raw || !options.cacheRegions) throw std::invalid_argument("structures require base callbacks and nonzero cacheRegions");
    impl_.reset(new Impl(seed,size,biome,ground,raw,options));
}
Generator::~Generator()=default;
Generator::Generator(Generator&&) noexcept=default;
Generator& Generator::operator=(Generator&&) noexcept=default;
std::vector<Block> Generator::generateForChunk(int cx,int cz) {
    auto& p=*impl_; std::lock_guard<std::mutex> lock(p.mutex);
    if(cx < -p.size/32 || cx>=p.size/32 || cz < -p.size/32 || cz>=p.size/32) {
        // Odd number-of-chunks worlds have partial chunks at both ends.
        if(int64_t(cx)*16+15 < -p.size/2 || int64_t(cx)*16>=p.size/2 ||
           int64_t(cz)*16+15 < -p.size/2 || int64_t(cz)*16>=p.size/2) return {};
    }
    std::map<std::tuple<int,int,int>,Block> writes;
    // Original MAX_DISTANCE=7*16; allow longest road, house/roof overhang too.
    const int reach=192;
    for(int rx=floorDiv(cx*16-reach,p.spacing*16);rx<=floorDiv(cx*16+15+reach,p.spacing*16);++rx)
        for(int rz=floorDiv(cz*16-reach,p.spacing*16);rz<=floorDiv(cz*16+15+reach,p.spacing*16);++rz) {
            auto& plan=p.plan(rx,rz);
            auto it=plan.chunks.find({cx,cz});
            if(it!=plan.chunks.end()) for(const auto& b:it->second) writes[{b.x,b.y,b.z}]=b;
        }
    for (const auto& b : p.scattered.generateForChunk(cx,cz)) writes[{b.x,b.y,b.z}]=b;
    std::vector<Block> result; result.reserve(writes.size());
    for(auto& b:writes) result.push_back(b.second);
    return result;
}
std::vector<Landmark> Generator::nearby(int x,int z,int radius) {
    auto& p=*impl_; std::lock_guard<std::mutex> lock(p.mutex);
    if(radius<0 || radius>p.size) throw std::invalid_argument("structures radius must be 0..worldSize");
    x=std::max(-p.size/2,std::min(p.size/2-1,x)); z=std::max(-p.size/2,std::min(p.size/2-1,z));
    std::vector<Landmark> result;
    int x0=std::max(-p.size/2,x-radius),x1=std::min(p.size/2-1,x+radius);
    int z0=std::max(-p.size/2,z-radius),z1=std::min(p.size/2-1,z+radius);
    for(int rx=floorDiv(x0,p.spacing*16);rx<=floorDiv(x1,p.spacing*16);++rx)
        for(int rz=floorDiv(z0,p.spacing*16);rz<=floorDiv(z1,p.spacing*16);++rz) {
            auto& plan=p.plan(rx,rz);const auto& l=plan.landmark;
            if(plan.valid && l.sourceX>=x0 && l.sourceX<=x1 && l.sourceZ>=z0 && l.sourceZ<=z1) result.push_back(l);
        }
    const auto scattered = p.scattered.nearby(x,z,radius);
    result.insert(result.end(), scattered.begin(), scattered.end());
    return result;
}
std::vector<uint8_t> Generator::emittedTileIDs() {
    std::vector<uint8_t> result{0,3,4,5,8,10,13,17,24,35,43,44,47,50,53,54,58,59,60,61,64,65,67,72,85,101,102,128,141,142};
    const auto scattered = ScatteredGenerator::emittedTileIDs();
    result.insert(result.end(), scattered.begin(), scattered.end());
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}
std::vector<uint8_t> Generator::unsupportedIDs(const std::function<bool(uint8_t)>& supported) {
    if(!supported) throw std::invalid_argument("supported predicate required");
    std::vector<uint8_t> result;for(auto id:emittedTileIDs()) if(!supported(id)) result.push_back(id);return result;
}
}
