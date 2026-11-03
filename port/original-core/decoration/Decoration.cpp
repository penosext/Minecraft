#include "Platform.h"
#include <list>

namespace original_decoration {
struct Generator::Impl {
    using Key=std::pair<int,int>;
    struct Entry { std::vector<Block> blocks; std::list<Key>::iterator lru; };
    int64_t seed;
    int size;
    std::function<int(int,int)> biome,ground;
    std::function<uint8_t(int,int,int)> raw;
    Options options;
    std::map<Key,Entry> cache;
    std::list<Key> recent;
    std::mutex mutex;
    Impl(int64_t s,int n,std::function<int(int,int)> b,std::function<int(int,int)> g,
         std::function<uint8_t(int,int,int)> r,Options o)
        : seed(s),size(n),biome(b),ground(g),raw(r),options(o) {}
    bool intersects(int cx,int cz) const {
        int64_t x=int64_t(cx)*16,z=int64_t(cz)*16;
        return x<size/2 && z<size/2 && x+16>-size/2 && z+16>-size/2;
    }
    const std::vector<Block>& get(int cx,int cz) {
        Key key(cx,cz); auto found=cache.find(key);
        if(found!=cache.end()) {
            recent.splice(recent.begin(),recent,found->second.lru);
            return found->second.blocks;
        }
        // The original samples origin+16. Finite outer-edge sample is clamped
        // to the last valid block; no callback ever receives an outside position.
        int bx=std::max(-size/2,std::min(size/2-1,cx*16+16));
        int bz=std::max(-size/2,std::min(size/2-1,cz*16+16));
        int id=biome(bx,bz);
        if(id<0 || id>22) throw std::out_of_range("decoration requires original biome IDs 0..22");
        std::vector<Block> blocks;
        if(id!=8 && id!=9) blocks=detail::plan(seed,size,cx,cz,id,ground,raw,options.plants);
        if(cache.size()>=options.cachePlans) {
            cache.erase(recent.back()); recent.pop_back();
        }
        recent.push_front(key);
        return cache.emplace(key,Entry{std::move(blocks),recent.begin()}).first->second.blocks;
    }
};
Generator::Generator(int64_t seed,int worldSize,std::function<int(int,int)> biome,
                     std::function<int(int,int)> ground,
                     std::function<uint8_t(int,int,int)> rawTile,Options options) {
    if(worldSize<=0 || worldSize%16 || worldSize>(1<<24) || !biome || !ground || !rawTile || !options.cachePlans)
        throw std::invalid_argument("decoration requires chunk-aligned finite world, callbacks and nonzero cache");
    impl_.reset(new Impl(seed,worldSize,biome,ground,rawTile,options));
}
Generator::~Generator() = default;
Generator::Generator(Generator&&) noexcept = default;
Generator& Generator::operator=(Generator&&) noexcept = default;
std::vector<Block> Generator::generateForChunk(int cx,int cz) {
    if(!impl_) throw std::logic_error("moved-from decoration Generator");
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if(!impl_->intersects(cx,cz)) return {};
    detail::Writes merged;
    // All supported feature extents fit origin and its eight neighboring chunks
    // (max origin+30 for plants; giant jungle branch/crown radius <=7).
    // A plan is never generated against another plan's writes, so query order,
    // cache eviction, and player edits cannot alter a tree or RNG stream.
    for(int ox=cx-1;ox<=cx+1;++ox) for(int oz=cz-1;oz<=cz+1;++oz) {
        if(!impl_->intersects(ox,oz)) continue;
        for(auto b:impl_->get(ox,oz)) {
            if(b.x<cx*16 || b.x>=cx*16+16 || b.z<cz*16 || b.z>=cz*16+16) continue;
            auto p=std::make_tuple(b.x,b.y,b.z);
            auto prior=merged.find(p);
            // Origin-plan merge rule, not an upstream postProcess claim: keep
            // trunks where independent crowns/plant patches overlap them.
            if(prior!=merged.end() && prior->second.tile==17 && b.tile!=17) continue;
            if(prior!=merged.end() && prior->second.tile==18 && b.tile!=17 && b.tile!=18) continue;
            merged[p]=b;
        }
    }
    std::vector<Block> result; result.reserve(merged.size());
    for(auto& entry:merged) result.push_back(entry.second);
    return result;
}
}
