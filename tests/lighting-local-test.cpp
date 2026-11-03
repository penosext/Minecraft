#include "World.hpp"
#include "WorldRenderer.hpp"
#include "LegacyCatalog.hpp"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
uint16_t blockId(const char* key) {
    for (int i = 0; i < legacy::kBlockCount; ++i)
        if (std::strcmp(key, legacy::kBlockSpecs[i].key) == 0) return uint16_t(i + 1);
    assert(false); return 0;
}
struct Fixture {
    int w, h, d;
    std::vector<uint16_t> cells;
    std::vector<uint8_t> loaded;
    mcworld::RenderVolume volume;
    Fixture(int width=48, int height=48, int depth=48)
        : w(width), h(height), d(depth), cells(size_t(w)*h*d), loaded(size_t(w)*d,1),
          volume{cells.data(),100,0,200,w,h,d,loaded.data()} {}
    size_t at(int x,int y,int z) const { return (size_t(y)*d+z)*w+x; }
    void put(int x,int y,int z,uint16_t value) { cells[at(x,y,z)]=value; }
    void roof(int y) { for(int z=0;z<d;++z)for(int x=0;x<w;++x)put(x,y,z,blockId("stone")); }
};
void settle(worldrender::LightingBuffer& b,const mcworld::RenderVolume& v,uint64_t revision) {
    int steps=0;
    while(!b.update(v,"fixture",revision,32)) {
        assert(b.pendingColumn>=0);
        assert(++steps<100000);
    }
    assert(b.pendingColumn<0 && b.revision==revision);
}
void unknownAndGeometry() {
    Fixture f; f.roof(20);
    for(int z=0;z<f.d;++z)for(int x=24;x<f.w;++x) {
        f.loaded[size_t(z)*f.w+x]=0;
        for(int y=0;y<f.h;++y)f.put(x,y,z,0);
    }
    f.put(25,8,24,blockId("torch")); // Unknown data must not emit, even if nonzero.
    worldrender::LightingBuffer b;
    assert(b.update(f.volume,"fixture",1,0));
    assert(b.sample(123,8,224).sky==0 && b.sample(123,8,224).block==0);
    assert(b.sample(125,8,224).sky==0 && b.sample(125,8,224).block==0);
    assert(b.sample(125,f.h+3,224).sky==0);
    assert(b.sample(105,f.h+3,205).sky==15);
    assert(b.sample(125,-1,224).sky==0);
    auto revision=b.revision;
    assert(b.refreshAround(f.volume,123,8,224,16));
    assert(b.revision==revision);
    assert(b.sample(125,8,224).sky==0 && b.sample(125,8,224).block==0);
    for(int z=0;z<f.d;++z)for(int x=24;x<f.w;++x) f.loaded[size_t(z)*f.w+x]=1;
    settle(b,f.volume,2);
    worldrender::LightingBuffer ref; assert(ref.update(f.volume,"fixture",2,0));
    assert(b.sky==ref.sky && b.block==ref.block);
    f.loaded[25*size_t(f.w)+25]=0;
    assert(b.sample(125,48,225).sky==0); // Old completed data cannot light an unloaded column.
    settle(b,f.volume,3);
    assert(ref.update(f.volume,"fixture",3,0));
    assert(b.sky==ref.sky && b.block==ref.block);
    Fixture small(16,24,24); small.roof(12);
    assert(b.update(small.volume,"fixture",4));
    worldrender::LightingBuffer smaller; assert(smaller.update(small.volume,"fixture",4,0));
    assert(b.sky==smaller.sky && b.block==smaller.block); // Layer stride must use new dimensions.
}
void localThenFull() {
    Fixture f; f.roof(20);
    worldrender::LightingBuffer b;
    assert(b.update(f.volume,"fixture",1,0));
    const auto before=b.sky;
    assert(b.sample(124,8,224).sky==0);
    f.put(24,20,24,0);
    assert(b.refreshAround(f.volume,124,20,224,16));
    assert(b.sample(124,8,224).sky>=13 && b.localRefreshes==1);
    assert(b.revision==1); // A patch never substitutes for whole-window completion.
    for(int z=0;z<f.d;++z)for(int x=0;x<f.w;++x)
        if(x<8 || x>40 || z<8 || z>40)for(int y=0;y<f.h;++y)
            assert(b.sky[f.at(x,y,z)]==before[f.at(x,y,z)]);
    assert(!b.update(f.volume,"fixture",2,1));
    assert(b.pendingColumn>=0);
    f.put(24,20,24,blockId("stone"));
    assert(b.refreshAround(f.volume,124,20,224,16));
    assert(b.sample(124,8,224).sky==0);
    settle(b,f.volume,3);
    worldrender::LightingBuffer ref; assert(ref.update(f.volume,"fixture",3,0));
    assert(b.sky==ref.sky && b.block==ref.block); // Interrupted old revision cannot replace the edit.
    f.put(24,8,24,blockId("torch"));
    assert(b.refreshAround(f.volume,124,8,224));
    assert(b.sample(124,8,224).block>=14);
    settle(b,f.volume,4);
    f.put(24,8,24,0);
    assert(b.refreshAround(f.volume,124,8,224));
    assert(b.sample(124,8,224).block==0);
    settle(b,f.volume,5); assert(ref.update(f.volume,"fixture",5,0));
    assert(b.sky==ref.sky && b.block==ref.block);
}
void viewOrderAndBudget() {
    Fixture f(64,64,64);
    worldrender::LightingBuffer b; assert(b.update(f.volume,"fixture",1,0));
    b.viewValid=true; b.viewEyeX=132; b.viewEyeZ=232; b.viewDirX=0; b.viewDirZ=1;
    assert(!b.update(f.volume,"fixture",2,1));
    for(int i=0;i<16;++i) assert(b.pendingOrder[size_t(i)]/64>=32);
    b.viewDirZ=-1;
    assert(!b.update(f.volume,"fixture",3,1));
    for(int i=0;i<16;++i) assert(b.pendingOrder[size_t(i)]/64<32);
    settle(b,f.volume,3);
    worldrender::LightingBuffer ref; assert(ref.update(f.volume,"fixture",3,0));
    assert(b.sky==ref.sky && b.block==ref.block);
    for(int i=0;i<10 && b.revision!=4;++i) b.update(f.volume,"fixture",4,2147483647);
    assert(b.revision==4); // Budget arithmetic must not overflow.
    assert(ref.update(f.volume,"fixture",4,0));
    assert(b.sky==ref.sky && b.block==ref.block);
}
}
int main() {
    unknownAndGeometry(); localThenFull(); viewOrderAndBudget();
    std::puts("lighting-local: unloaded masking, new strides, immediate edit, bounded patch, pending restart and full convergence passed");
}
