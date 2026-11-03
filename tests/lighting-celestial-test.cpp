// Standalone native test. Link renderer_runtime/world_runtime and the original
// static libraries; generated lighting/sky headers live under port/assets/sky.
#define WORLD_RENDERER_REFERENCE_ONLY
#include "world-renderer-test.cpp"
#include "../port/assets/sky/SkyTextures.hpp"
#include <array>
#include <iomanip>
#include <numeric>

namespace {
using namespace worldrender;
uint16_t id(std::string_view key) {
    for(int i=0;i<legacy::kBlockCount;++i)if(key==legacy::kBlockSpecs[i].key)return uint16_t(i+1);
    std::cerr<<"Missing test block "<<key<<'\n';std::abort();
}
struct Fixture {
    std::vector<uint16_t> cells;
    mcworld::RenderVolume volume;
    LightingSettings settings;
    Fixture(int w=40,int h=32,int d=40):cells(size_t(w)*h*d),volume{cells.data(),100,20,300,w,h,d} {
        settings.cachePath="lighting-fixture";settings.revision=1;
    }
    void put(int x,int y,int z,uint16_t tile) { cells[(size_t(y)*volume.depth+z)*volume.width+x]=tile; }
    void roof(int y,uint16_t tile) {
        for(int z=0;z<volume.depth;++z)for(int x=0;x<volume.width;++x)put(x,y,z,tile);
    }
    // A revision change is rebuilt incrementally across calls, so these
    // correctness checks drive it to convergence instead of assuming one call.
    // Returns whether anything was rebuilt at all.
    static bool settle(LightingBuffer& buffer, const mcworld::RenderVolume& volume,
                       std::string_view path, uint64_t revision) {
        bool built=false;
        for(int step=0;step<100000;++step) {
            if(buffer.update(volume,path,revision)) { built=true; break; }
            if(buffer.pendingColumn<0) break; // nothing in flight: genuinely up to date
        }
        assert(buffer.pendingColumn<0);
        return built;
    }
    std::vector<uint32_t> image(float daylight=1,bool ao=true,Point eye={120.5f,23.5f,320.5f},float pitch=-30) {
        std::vector<uint32_t> pixels(161*101);
        settings.ambientOcclusion=ao;
        // renderVolumeFrame advances the incremental rebuild by one budget per
        // call, so drive the shared render cache to convergence before judging
        // pixels; otherwise the image legitimately shows the previous lighting.
        for(int step=0;step<100000;++step) {
            renderVolumeFrame(pixels.data(),161,101,volume,eye,0,pitch,1,daylight,-1,nullptr,settings);
            if(lightingBuffer().pendingColumn<0) break;
        }
        renderVolumeFrame(pixels.data(),161,101,volume,eye,0,pitch,1,daylight,-1,nullptr,settings);
        return pixels;
    }
};
double brightness(const std::vector<uint32_t>& pixels) {
    double sum=0;size_t count=0;
    for(auto p:pixels)if(p!=0xffeeeeee){sum+=((p>>16)&255)+((p>>8)&255)+(p&255);++count;}
    return sum/(3*count);
}
void tileRules() {
    for(const auto& test:std::array<std::pair<const char*,int>,8>{{{"stone",255},{"water",3},{"calmWater",3},
        {"leaves_oak",1},{"glass",0},{"stairs_wood",255},{"stoneSlabHalf",255},{"lava",255}}}) {
        // The catalog's existing slab key is platform-version dependent; test
        // slab properties below by source id instead of weakening coverage.
        if(std::string_view(test.first)=="stoneSlabHalf")continue;
        assert(tileLighting(id(test.first)).lightBlock==test.second);
    }
    assert(tileLighting(id("torch")).emission==14);
    assert(tileLighting(id("lava")).emission==15);
    assert(tileLighting(id("glowstone")).emission==15);
    assert(tileLighting(65535).lightBlock==0);
    for(int i=0;i<legacy::kBlockCount;++i)if(legacy::kBlockSpecs[i].tileId==44)
        assert(tileLighting(uint16_t(i+1)).lightBlock==255);
}
void roofCaveAndCache() {
    Fixture f;
    f.roof(1,id("stone"));
    LightingBuffer b;
    assert(Fixture::settle(b,f.volume,"cave",1));
    assert(b.sample(120,22,320).sky==15);
    const auto open=f.image();
    f.roof(8,id("stone"));++f.settings.revision;
    assert(Fixture::settle(b,f.volume,"cave",2));
    assert(b.sample(120,22,320).sky==0);
    const auto dark=f.image();
    assert(brightness(dark)<brightness(open)*.25);
    f.put(20,8,20,0);++f.settings.revision;
    assert(Fixture::settle(b,f.volume,"cave",3));
    assert(b.sample(120,22,320).sky==15);
    assert(b.sample(121,22,320).sky==14);
    assert(b.sample(122,22,320).sky==13);
    const auto hole=f.image();
    assert(brightness(hole)>brightness(dark)*2);
    const auto n=b.builds;
    const auto* sky=b.sky.data();const auto* block=b.block.data();
    assert(!b.update(f.volume,"cave",3)&&b.builds==n);
    assert(b.sky.data()==sky&&b.block.data()==block&&sky!=block);
    assert(b.sky.size()+b.block.size()==2*f.cells.size());
    assert(b.update(f.volume,"other-world",3)&&b.builds==n+1);
    auto shifted=f.volume;++shifted.originX;
    assert(Fixture::settle(b,shifted,"other-world",3));
    auto movedY=shifted;++movedY.originY;
    assert(Fixture::settle(b,movedY,"other-world",3));
    auto movedZ=movedY;++movedZ.originZ;
    assert(Fixture::settle(b,movedZ,"other-world",3));
    auto copied=f.cells;auto replacement=movedZ;replacement.data=copied.data();
    assert(Fixture::settle(b,replacement,"other-world",3));
    assert(b.sample(0,0,0).sky==0);
    assert(b.sample(replacement.originX,replacement.originY+replacement.height,replacement.originZ).sky==15);
    for(auto value:b.sky)assert(value<=15);
    std::cout<<"Roof/cave: open="<<brightness(open)<<" covered="<<brightness(dark)<<" mined-hole="<<brightness(hole)<<'\n';
}
void transparentAttenuation() {
    Fixture f(9,16,9);LightingBuffer b;
    for(const auto& test:std::array<std::pair<const char*,int>,4>{{{"water",12},{"leaves_oak",14},{"glass",15},{"ice",12}}}) {
        std::fill(f.cells.begin(),f.cells.end(),0);
        f.roof(12,id(test.first));
        assert(Fixture::settle(b,f.volume,"transparent",++f.settings.revision));
        assert(b.sample(104,32,304).sky==test.second);
        assert(b.sample(104,31,304).sky==(test.second==15?15:test.second-1));
    }
    // Closed lateral boundary: a completely blocked column does not receive
    // fabricated outside-window sunlight, and linear indices never wrap.
    std::fill(f.cells.begin(),f.cells.end(),id("stone"));
    for(int y=1;y<8;++y)f.put(0,y,4,0);
    assert(Fixture::settle(b,f.volume,"edge",1));
    assert(b.sample(100,24,304).sky==0);
}
void emissionAndDayNight() {
    Fixture f;f.roof(1,id("stone"));f.roof(8,id("stone"));
    const auto dark=f.image(.15f);
    f.put(20,3,322-300,id("torch"));++f.settings.revision;
    LightingBuffer b;assert(Fixture::settle(b,f.volume,"torch",1));
    assert(b.sample(120,23,322).block==14);
    assert(b.sample(121,23,322).block==13);
    assert(b.sample(122,23,322).block==12);
    const auto night=f.image(.15f),day=f.image(1);
    assert(brightness(night)>brightness(dark)*2);
    // Close cave floor, before fog/window exit: block light does not dim with
    // the sun. Far rays may leave the bounded window and see sky/fog instead.
    for(int y=60;y<101;++y)for(int x=50;x<111;++x)assert(night[y*161+x]==day[y*161+x]);
    f.put(20,3,22,id("lava"));++f.settings.revision;
    assert(Fixture::settle(b,f.volume,"torch",2));
    assert(b.sample(120,23,322).block==15&&b.sample(121,23,322).block==14);
    f.put(20,3,22,id("glowstone"));++f.settings.revision;
    assert(Fixture::settle(b,f.volume,"torch",3));
    assert(b.sample(120,23,322).block==15&&b.sample(121,23,322).block==14);
    std::fill(f.cells.begin(),f.cells.end(),0);f.roof(1,id("stone"));++f.settings.revision;
    const auto outdoorDay=f.image(1),outdoorNight=f.image(.15f);
    assert(brightness(outdoorNight)<brightness(outdoorDay)*.3);
    std::cout<<"Torch cave: dark="<<brightness(dark)<<" torch-night="<<brightness(night)<<'\n';
}
void aoAndGuardedBuffers() {
    Fixture f;f.roof(1,id("stone"));
    for(int z=18;z<30;++z)f.put(21,2,z,id("stone"));
    const auto flat=f.image(1,false),ao=f.image(1,true);
    assert(flat!=ao&&brightness(ao)<brightness(flat));
    constexpr uint32_t guard=0x12345678;
    for(int w:{1,2,7,31})for(int h:{1,3,14})for(int step:{0,1,3,7,100}) {
        std::vector<uint32_t> pixels(size_t(w)*h+2,guard);
        renderVolumeFrame(pixels.data()+1,w,h,f.volume,{120.5f,23.5f,320.5f},17,-20,step,1,-1,nullptr,f.settings);
        assert(pixels.front()==guard&&pixels.back()==guard);
        for(size_t i=1;i+1<pixels.size();++i)assert((pixels[i]>>24)==255);
    }
    uint32_t untouched=guard;
    renderVolumeFrame(&untouched,1,1,f.volume,{0,0,0},0,0);
    renderVolumeFrame(&untouched,1,1,f.volume,{120,23,320},NAN,0);
    assert(untouched==guard);
}
Point rotated(Point p,float phase) {
    const float a=phase*6.283185307179586f,c=std::cos(a),s=std::sin(a);
    // Exact forward matrix Ry(-90)*Rx(a).
    return {-s*p.y-c*p.z,c*p.y-s*p.z,p.x};
}
void originalQuadTextures() {
    assert(verticalFov()==70);
    assert(std::abs(focalLength(960,266)-266/(2*std::tan(35*3.141592653589793/180)))<.001);
    assert(focalLength(960,266)==focalLength(480,266)); // vertical, not horizontal FOV
    for(float phase:{0.f,.13f,.25f,.5f,.75f,.91f}) {
        for(int y=0;y<32;++y)for(int x=0;x<32;++x) {
            const Point p{-30+(x+.5f)*60/32,100,-30+(y+.5f)*60/32};
            assert(celestialTexel(rotated(p,phase),phase)==skytextures::kSunPixels[y*32+x]);
        }
        for(int phaseIndex=0;phaseIndex<8;++phaseIndex)
            for(int y=0;y<32;++y)for(int x=0;x<32;++x) {
                const Point p{20-(x+.5f)*40/32,-100,-20+(y+.5f)*40/32};
                const uint32_t expected=skytextures::kMoonPixels[(phaseIndex/4*32+y)*128+phaseIndex%4*32+x];
                assert(celestialTexel(rotated(p,phase),phase,phaseIndex)==expected);
            }
        assert(celestialTexel(rotated({30.1f,100,0},phase),phase)==0);
        assert(celestialTexel(rotated({0,-100,20.1f},phase),phase)==0);
        // A square corner is INSIDE the source quad, even beyond a same-sized circle.
        assert(celestialTexel(rotated({29,100,29},phase),phase)==skytextures::kSunPixels[31*32+31]);
    }
    Fixture f;
    constexpr int w=401,h=301;
    std::vector<uint32_t> plain(w*h),sun(w*h);
    renderVolumeFrame(plain.data(),w,h,f.volume,{120.5f,35,320.5f},270,0,1,1,-1,nullptr,f.settings);
    renderVolumeFrame(sun.data(),w,h,f.volume,{120.5f,35,320.5f},270,0,1,1,.25f,nullptr,f.settings);
    int minX=w,minY=h,maxX=-1,maxY=-1;
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        const auto i=y*w+x;
        if(sun[i]!=plain[i]&&sun[i]==0xffffffff) {
            minX=std::min(minX,x);maxX=std::max(maxX,x);minY=std::min(minY,y);maxY=std::max(maxY,y);
        }
    }
    assert(maxX>=minX&&maxY>=minY);
    assert(std::abs((maxX-minX)-(maxY-minY))<=1); // bright square, not disk
    // Every corner of the bright square is lit. Analytic circles fail this.
    assert(sun[minY*w+minX]==0xffffffff&&sun[maxY*w+maxX]==0xffffffff);
    f.roof(22,id("stone"));++f.settings.revision;
    std::vector<uint32_t> covered(w*h),coveredNoSun(w*h);
    renderVolumeFrame(covered.data(),w,h,f.volume,{120.5f,35,320.5f},0,80,1,1,0,nullptr,f.settings);
    renderVolumeFrame(coveredNoSun.data(),w,h,f.volume,{120.5f,35,320.5f},0,80,1,1,-1,nullptr,f.settings);
    assert(covered==coveredNoSun); // opaque geometry always occludes sky texture
    std::cout<<"Original textured sun bright square="<<maxX-minX+1<<'x'<<maxY-minY+1<<"; all 8 moon phases byte-exact\n";
}
void insetShapeLighting() {
    uint16_t slab=0;
    for(int i=0;i<legacy::kBlockCount;++i)
        if(legacy::kBlockSpecs[i].tileId==44&&legacy::kBlockSpecs[i].data==0)slab=uint16_t(i+1);
    assert(slab);
    Fixture f;f.roof(1,slab);
    const auto day=f.image(1,false),night=f.image(.15f,false);
    double lit=0,dark=0;size_t count=0;
    for(int y=70;y<101;++y)for(int x=40;x<121;++x) {
        const auto a=day[y*161+x],b=night[y*161+x];
        lit+=((a>>16)&255)+((a>>8)&255)+(a&255);
        dark+=((b>>16)&255)+((b>>8)&255)+(b&255);++count;
    }
    assert(lit/(3*count)>50); // inset top is neighbour-lit, NOT black solid cell
    assert(dark<lit*.3);
}
void fortyBlockRange() {
    Fixture f(96,32,96);
    std::vector<uint32_t> sky(161*101),near(sky.size()),far(sky.size());
    const Point eye{148.5f,23.5f,348.5f};
    renderVolumeFrame(sky.data(),161,101,f.volume,eye,0,0,1,1,-1,nullptr,f.settings);
    for(int y=0;y<32;++y)for(int x=0;x<96;++x)f.put(x,y,88,id("stone"));
    ++f.settings.revision;
    renderVolumeFrame(near.data(),161,101,f.volume,eye,0,0,1,1,-1,nullptr,f.settings);
    assert(near!=sky); // 39.5 blocks, still drawn, including source shape lighting
    for(int y=0;y<32;++y)for(int x=0;x<96;++x){f.put(x,y,88,0);f.put(x,y,89,id("stone"));}
    ++f.settings.revision;
    renderVolumeFrame(far.data(),161,101,f.volume,eye,0,0,1,1,-1,nullptr,f.settings);
    assert(far==sky); // 40.5 blocks; normalized physical range, not per-ray t
}
void frozenOptOut() {
    char path[]="/tmp/mc-light-frozen.XXXXXX";assert(mkdtemp(path));
    mcworld::World world;world.create(12345,path,false);
    auto* voxels=const_cast<uint16_t*>(world.blockData());
    const size_t count=size_t(world.width())*world.height()*world.depth();
    std::fill(voxels,voxels+count,id("stone"));
    const int x=int(world.playerX()),y=int(world.playerY()+1.62f),z=int(world.playerZ());
    voxels[(size_t(y)*world.depth()+z)*world.width()+x]=0;
    LightingSettings settings;settings.enabled=false;settings.ambientOcclusion=false;settings.legacyProjection=true;
    for(float yaw:{0.f,90.f,180.f,270.f}) {
        std::vector<uint32_t> a(163*91),b(a.size());
        renderWorldFrame(a.data(),163,91,world,yaw,-11,2,1,-1,settings);
        renderer_reference::render(b.data(),163,91,world,yaw,-11,2);
        assert(renderer_reference::difference(a,b).maxChannel<=1);
    }
    std::filesystem::remove_all(path);
}
void worldMiningInvalidation() {
    char path[]="/tmp/mc-light-mine.XXXXXX";assert(mkdtemp(path));
    mcworld::World world;world.create(54321,path,false);
    const auto& volume=world.renderVolume();
    auto* voxels=const_cast<uint16_t*>(volume.data);
    const size_t count=size_t(volume.width)*volume.height*volume.depth;
    std::fill(voxels,voxels+count,0);
    const int ex=int(world.playerX()),ey=int(world.playerY()+1.62f),ez=int(world.playerZ());
    assert(ey>=2&&ey+2<volume.height);
    for(int z=0;z<volume.depth;++z)for(int x=0;x<volume.width;++x) {
        voxels[(size_t(ey-2)*volume.depth+z)*volume.width+x]=id("stone");
        voxels[(size_t(ey+2)*volume.depth+z)*volume.width+x]=id("stone");
    }
    std::vector<uint32_t> before(161*101),after(before.size());
    // The roof/floor above were written straight into the volume, so World's
    // revision never changed and the shared cache would be reused unmodified.
    // Discard it explicitly, settle the rebuild, then draw once more so the
    // measured image uses the finished lighting instead of the frame that only
    // completed it. Comparing an unconverged frame would measure rebuild
    // progress rather than the mined skylight.
    const_cast<LightingBuffer&>(lightingBuffer()) = LightingBuffer{};
    for(int step=0;step<100000;++step) {
        renderWorldFrame(before.data(),161,101,world,0,-40,2);
        if(lightingBuffer().pendingColumn<0) break;
    }
    renderWorldFrame(before.data(),161,101,world,0,-40,2);
    const auto revision=world.revision(),builds=lightingBuffer().builds;
    const auto localRefreshes=lightingBuffer().localRefreshes;
    assert(lightingBuffer().sample(ex,ey,ez).sky==0);
    assert(world.mine(0,80,5));
    assert(world.revision()!=revision);
    renderWorldFrame(after.data(),161,101,world,0,-40,2);
    assert(lightingBuffer().localRefreshes>localRefreshes);
    assert(lightingBuffer().sample(ex,ey,ez).sky>=13);
    assert(brightness(after)>brightness(before)*2);
    // The immediate patch must still be followed by a complete budgeted rebuild.
    for(int step=0;step<100000;++step) {
        renderWorldFrame(after.data(),161,101,world,0,-40,2);
        if(lightingBuffer().pendingColumn<0) break;
    }
    assert(lightingBuffer().builds==builds+1);
    assert(lightingBuffer().revision==world.revision());
    assert(lightingBuffer().sample(ex,ey,ez).sky>=13);
    // The frame that completed the rebuild still sampled the old data, so draw
    // one more now that the new lighting is published.
    renderWorldFrame(after.data(),161,101,world,0,-40,2);
    assert(brightness(after)>brightness(before)*2);
    renderWorldFrame(after.data(),161,101,world,15,-40,2);
    assert(lightingBuffer().builds==builds+1); // yaw doesn't invalidate lighting
    std::filesystem::remove_all(path);
}
void performance() {
    Fixture f(96,256,96);f.roof(62,id("stone"));
    for(int z=35;z<70;++z)for(int x=35;x<70;++x) {
        if((x+z)%5==0)f.put(x,63,z,id("stairs_wood"));
        if((x+z)%13==0)f.put(x,64,z,id("torch"));
    }
    for(int z=35;z<70;++z)for(int x=35;x<70;++x)if(x<47||x>50)f.put(x,70,z,id("stone"));
    const Point eye{148.5f,85.5f,348.5f};
    std::vector<uint32_t> pixels(960*266+2,0x12345678);
    const auto start=std::chrono::steady_clock::now();
    renderVolumeFrame(pixels.data()+1,960,266,f.volume,eye,0,-10,3,1,.1f,nullptr,f.settings);
    const double cold=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    const auto builds=lightingBuffer().builds;
    const auto* sky=lightingBuffer().sky.data();const auto* block=lightingBuffer().block.data();
    for(int i=0;i<3;++i)renderVolumeFrame(pixels.data()+1,960,266,f.volume,eye,0,-10,3,1,.1f,nullptr,f.settings);
    const auto warmStart=std::chrono::steady_clock::now();
    for(int i=0;i<20;++i)renderVolumeFrame(pixels.data()+1,960,266,f.volume,eye,float(i%3),-10,3,i%2?.15f:1,.1f,nullptr,f.settings);
    const double warm=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-warmStart).count()/20;
    assert(lightingBuffer().builds==builds);
    assert(lightingBuffer().sky.data()==sky&&lightingBuffer().block.data()==block);
    assert(lightingBuffer().sky.size()+lightingBuffer().block.size()==2*f.cells.size());
    assert(pixels.front()==0x12345678&&pixels.back()==0x12345678);
    std::cout<<std::fixed<<std::setprecision(3)<<"96x256x96 light bytes="<<2*f.cells.size()
             <<" cold960x266="<<cold<<"ms warm step3="<<warm<<"ms target<20ms "<<(warm<20?"PASS":"MISSED")<<'\n';
    // Timing is reported, not a functional gate: concurrent world-generation
    // tests and sanitizers can legitimately exceed the device/frame target.
}
}
int main() {
    tileRules();roofCaveAndCache();transparentAttenuation();emissionAndDayNight();
    aoAndGuardedBuffers();originalQuadTextures();insetShapeLighting();fortyBlockRange();frozenOptOut();worldMiningInvalidation();performance();
    std::cout<<"Lighting/celestial passed: roof, mined hole, attenuation, emission, day/night, AO, exact quads, buffers and cache\n";
}
