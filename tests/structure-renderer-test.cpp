// Native host regression. Link World, OriginalWorld, WorldRenderer, LegacyBlock,
// SpecialShapes and the original_{structures,terrain,biomes,core} libraries.
#define WORLD_RENDERER_REFERENCE_ONLY
#include "world-renderer-test.cpp"
#include <array>
#include <iomanip>

namespace {
using worldrender::Point;
using worldrender::Geometry;
using worldrender::Hit;
constexpr uint32_t guard = 0x12345678;
const legacy::BlockSpec& spec(std::string_view key) {
    for (const auto& b : legacy::kBlockSpecs) if (key == b.key) return b;
    std::abort();
}
uint16_t id(std::string_view key) { return uint16_t(&spec(key) - legacy::kBlockSpecs + 1); }
uint16_t variant(std::string_view key, int data) {
    const auto name = std::string(key) + "_data_" + std::to_string(data);
    for (int i=0;i<legacy::kBlockCount;++i) if (name==legacy::kBlockSpecs[i].key) return uint16_t(i+1);
    return 0; // Raw-spec geometry tests below do not require generated variants.
}
bool hit(const Geometry& g, Point o, Point d, Hit* result=nullptr) {
    Hit value;
    const bool found=worldrender::rayIntersection(g,o,d,0,20,value);
    if (found && result) *result=value;
    return found;
}
std::vector<uint32_t> geometryImage(const Geometry& g) {
    // Fixed oblique perspective, not an occupancy bitmap: internal riser UVs,
    // upper/lower silhouettes and distance all affect the rendered image.
    constexpr int w=101,h=87;
    std::vector<uint32_t> result(w*h,0xffaaccff);
    for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
        const float u=(x+.5f)/w-.5f,v=.5f-(y+.5f)/h;
        Hit p;
        if(hit(g,{-1.4f,1.7f,-2},{.6f+u*.8f,-.4f+v*.7f,1},&p))
            result[y*w+x]=ARGB(255,40+p.face*30,int(std::clamp(p.u,0.0f,1.0f)*255),
                                            int(std::clamp(p.v,0.0f,1.0f)*255));
    }
    return result;
}
void directMetadata() {
    auto stairs=spec("stairs_wood");
    std::array<std::vector<uint32_t>,8> pictures;
    for(int data=0;data<8;++data) {
        stairs.data=data;
        const auto g=worldrender::buildGeometry(stairs);
        assert(!g.fullCube && g.boxes.size()==2);
        double volume=0;
        for(const auto& b:g.boxes) volume+=(b.high.x-b.low.x)*(b.high.y-b.low.y)*(b.high.z-b.low.z);
        assert(volume==.75);
        for(int x=0;x<2;++x) for(int z=0;z<2;++z) {
            const float px=.25f+.5f*x,pz=.25f+.5f*z;
            Hit h;
            assert(hit(g,{px,2,pz},{0,-1,0},&h));
            const bool raised=(data&3)==0?x==1:(data&3)==1?x==0:(data&3)==2?z==1:z==0;
            assert(std::abs(h.distance-((data&4)||raised?1.0f:1.5f))<1e-6f);
            // Horizontal rays through the absent half must pass right through.
            const float py=(data&4)?.25f:.75f;
            if((data&3)<2) assert(hit(g,{px,py,-1},{0,0,1})==raised);
            else assert(hit(g,{-1,py,pz},{1,0,0})==raised);
        }
        pictures[data]=geometryImage(g);
        const auto mesh=worldrender::buildShapeMesh(stairs);
        assert(mesh.size()==48);
        for(const auto& v:mesh) assert(v.x>=0&&v.x<=1&&v.y>=0&&v.y<=1&&v.z>=0&&v.z<=1);
    }
    for(int i=0;i<8;++i) for(int j=0;j<i;++j) assert(pictures[i]!=pictures[j]);
    auto slab=stairs;slab.shape="slab";
    slab.data=0;
    auto lower=worldrender::buildGeometry(slab);
    assert(!hit(lower,{.2f,.75f,-1},{0,0,1}));
    Hit h;
    assert(hit(lower,{.2f,.25f,-1},{0,0,1},&h));
    assert(h.face==2&&std::abs(h.v-.75f)<1e-6f); // cropped, not stretched full-height UV
    slab.data=8;
    auto upper=worldrender::buildGeometry(slab);
    assert(hit(upper,{.2f,.75f,-1},{0,0,1}));
    assert(!hit(upper,{.2f,.25f,-1},{0,0,1}));
    assert(geometryImage(lower)!=geometryImage(upper));
    auto door=spec("door_wood_lower");
    std::array<std::vector<uint32_t>,4> doors;
    for(int data=0;data<8;++data) {
        door.data=data;
        auto g=worldrender::buildGeometry(door);
        assert(g.door&&g.boxes.size()==1);
        const auto b=g.boxes.front();
        assert((b.high.x-b.low.x)*(b.high.z-b.low.z)==3/16.0f);
        if(data<4)doors[data]=geometryImage(g);
        if((data&3)%2==0 && !(data&4)) assert(!hit(g,{.5f,.5f,-1},{0,0,1}));
        if((data&3)%2==1 && !(data&4)) assert(!hit(g,{-1,.5f,.5f},{1,0,0}));
    }
    for(int i=0;i<4;++i)for(int j=0;j<i;++j)assert(doors[i]!=doors[j]);
    for(const char* shape:{"pane","fence","rail","torch","ladder","cross","crops"}) {
        auto b=stairs;b.shape=shape;b.data=5;
        const auto g=worldrender::buildGeometry(b);
        assert(!g.fullCube&&(!g.boxes.empty()||!g.planes.empty()));
        if(std::string_view(shape)=="fence"||std::string_view(shape)=="torch")
            assert(!hit(g,{.1f,.99f,-1},{0,0,1}));
    }
    auto ladder=stairs;ladder.shape="ladder";
    for(int d=2;d<=5;++d) {
        ladder.data=d;auto g=worldrender::buildGeometry(ladder);
        assert(g.planes.size()==1);
        assert(hit(g,d<=3?Point{.5f,.5f,-1}:Point{-1,.5f,.5f},d<=3?Point{0,0,1}:Point{1,0,0}));
    }
    // Inside the L's missing quadrant, first surface is the internal riser.
    stairs.data=0;
    assert(hit(worldrender::buildGeometry(stairs),{.2f,.75f,.4f},{1,0,0},&h));
    assert(h.face==4&&std::abs(h.distance-.3f)<1e-6f&&std::abs(h.v-.25f)<1e-6f);
    assert(hit(worldrender::buildGeometry(stairs),{.75f,.25f,.4f},{0,1,0},&h));
    assert(h.face==1&&std::abs(h.distance-.75f)<1e-6f); // no seam at y=.5
    auto crop=stairs;crop.shape="crops";
    const auto rows=worldrender::buildGeometry(crop);
    assert(hit(rows,{.5f,.5f,-1},{0,0,1},&h));
    const float first=h.distance;
    assert(worldrender::rayIntersection(rows,{.5f,.5f,-1},{0,0,1},first+.001f,3,h));
    assert(h.distance>first); // cutout first plane can reveal the next same-cell plane
}
struct Fixture {
    static constexpr int w=16,h=12,d=16;
    std::vector<uint16_t> cells=std::vector<uint16_t>(w*h*d);
    mcworld::RenderVolume volume{cells.data(),100,20,300,w,h,d};
    size_t at(int x,int y,int z) {return (size_t(y)*d+z)*w+x;}
    void put(int x,int y,int z,uint16_t value){cells[at(x,y,z)]=value;}
    std::vector<uint32_t> render(Point eye={107.5f,23.75f,303.5f},int width=101,int height=31,
                                 float yaw=0,float pitch=0,float daylight=1) {
        std::vector<uint32_t> p(size_t(width)*height+2,guard);
        // Fixed DDA pixel indices predate vertical FOV, skylight and AO.
        worldrender::LightingSettings lighting;
        lighting.enabled=false;lighting.ambientOcclusion=false;lighting.legacyProjection=true;
        worldrender::renderVolumeFrame(p.data()+1,width,height,volume,eye,yaw,pitch,1,daylight,-1,nullptr,lighting);
        assert(p.front()==guard&&p.back()==guard);
        for(size_t i=1;i+1<p.size();++i)assert(p[i]>>24==255);
        return p;
    }
};
void ddaChecks() {
    Fixture f;
    for(int x=0;x<16;++x)for(int y=0;y<12;++y)f.put(x,y,8,id("stone"));
    const auto background=f.render({107.5f,23.75f,303.5f},101,1);
    const auto low=variant("stairs_wood",0), high=variant("stairs_wood",4);
    if(low&&high) {
        std::array<std::vector<uint32_t>,8> images;
        for(int data=0;data<8;++data) {
            f.put(7,3,5,variant("stairs_wood",data));
            images[data]=f.render({106.8f,24.0f,303.5f},121,61,15,-6);
        }
        for(int i=0;i<8;++i)for(int j=0;j<i;++j)assert(images[i]!=images[j]);
        f.put(7,3,5,low);
        const auto gaps=f.render({107.2f,23.75f,303.5f},101,1);
        f.put(7,3,5,0);
        const auto empty=f.render({107.2f,23.75f,303.5f},101,1);
        assert(gaps[45]==empty[45]); // misses raised east half, sees the wall behind
        f.put(7,3,5,high);
        assert(f.render({107.2f,23.75f,303.5f},101,1)[45]!=empty[45]);
    }
    f.put(7,3,5,0);
    const auto lower=variant("door_wood_lower",0), top=variant("door_wood_upper",8);
    if(lower&&top) {
        f.put(7,3,5,lower);f.put(7,4,5,top);
        const auto west=f.render({107.5f,24.5f,303.5f});
        f.put(7,3,5,variant("door_wood_lower",1));
        const auto north=f.render({107.5f,24.5f,303.5f});
        assert(west!=north); // upper orientation is inherited from lower metadata
        f.put(7,3,5,variant("door_wood_lower",4));
        const auto left=f.render({107.5f,24.5f,303.5f});
        f.put(7,4,5,variant("door_wood_upper",9));
        assert(left!=f.render({107.5f,24.5f,303.5f})); // upper hinge affects both halves
    }
    // Window guards: only the very last x-column is occupied. A wrapped linear
    // index on crossing the opposite x face would incorrectly see it.
    std::fill(f.cells.begin(),f.cells.end(),0);
    const auto sky=f.render({100.1f,23.5f,307.5f},97,19,270);
    for(int z=0;z<16;++z)for(int y=0;y<12;++y)f.put(15,y,z,id("stone"));
    assert(sky==f.render({100.1f,23.5f,307.5f},97,19,270));
    if(top) { // upper door on the lowest resident y row must not read below array
        f.put(0,0,7,top);f.render({100.5f,20.5f,306.5f},17,7);
    }
    // Sparse leaves keep alpha rejection and reveal the next cell, not a solid
    // box silhouette. Compare against the same patch backed by stone.
    std::fill(f.cells.begin(),f.cells.end(),0);
    const auto air=f.render();
    f.put(7,3,5,id("leaves_oak"));
    const auto leaf=f.render();
    f.put(7,3,5,id("stone"));
    const auto solid=f.render();
    size_t holes=0,opaque=0;
    for(size_t i=0;i<air.size();++i) {
        holes+=leaf[i]==air[i]&&solid[i]!=air[i];
        opaque+=leaf[i]!=air[i];
    }
    assert(holes>0&&opaque>0);
    (void)background;
}
void cubeRegressionAndPerformance() {
    char path[]="/tmp/mc-structure-renderer.XXXXXX";
    assert(mkdtemp(path));
    mcworld::World world;world.create(12345,path,false);
    auto* voxels=const_cast<uint16_t*>(world.renderVolume().data);
    const size_t cells=size_t(mcworld::Width)*mcworld::Depth*mcworld::Height;
    const int ex=int(world.playerX()),ey=int(world.playerY()+1.62f),ez=int(world.playerZ());
    const auto cell=[](int x,int y,int z){return (size_t(y)*mcworld::Depth+z)*mcworld::Width+x;};
    // All hits are close, before fog; frozen reference's old unnormalised far
    // range has no bearing on the fullcube equality check. Explicitly opt out
    // of modern projection/lighting only for this frozen day/night regression.
    worldrender::LightingSettings lighting;
    lighting.enabled=false;lighting.ambientOcclusion=false;lighting.legacyProjection=true;
    for(const char* key:{"stone","grass","leaves_oak"}) {
        std::fill(voxels,voxels+cells,id(key));voxels[cell(ex,ey,ez)]=0;
        for(float yaw:{0.f,90.f,180.f,270.f}) {
            std::vector<uint32_t> a(163*91),b(a.size()),night(a.size());
            renderWorldFrame(a.data(),163,91,world,yaw,-11,2,1,-1,lighting);
            renderer_reference::render(b.data(),163,91,world,yaw,-11,2);
            assert(renderer_reference::difference(a,b).maxChannel<=1);
            renderWorldFrame(night.data(),163,91,world,yaw,-11,2,.2f,-1,lighting);
            for(size_t i=0;i<a.size();++i)if(a[i]!=0xffeeeeee)for(int shift:{0,8,16})
                assert(std::abs(int((night[i]>>shift)&255)-int(((a[i]>>shift)&255)*.2f))<=1);
        }
    }
    std::vector<uint32_t> pixels(960*266);
    const std::array<const char*,3> labels={"cube-floor","stairs-floor","mixed-structure"};
    for(int scene=0;scene<3;++scene) {
        std::fill(voxels,voxels+cells,0);
        const int floor=std::max(0,ey-2);
        for(int z=0;z<mcworld::Depth;++z)for(int x=0;x<mcworld::Width;++x) {
            uint16_t block=id("stone");
            if(scene>0) {const auto value=variant("stairs_wood",(x+z)&7);if(value)block=value;}
            voxels[cell(x,floor,z)]=block;
            if(scene==2&&((x+z)%5)==0)voxels[cell(x,floor+1,z)]=id((x%2)?"fence":"torch");
        }
        for(int i=0;i<3;++i)renderWorldFrame(pixels.data(),960,266,world,0,-10,3);
        const auto start=std::chrono::steady_clock::now();
        constexpr int frames=12;
        for(int i=0;i<frames;++i)renderWorldFrame(pixels.data(),960,266,world,0,-10,3);
        const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/frames;
        std::cout<<labels[scene]<<" default 960x266 step=3 ms/frame="<<std::fixed<<std::setprecision(3)<<ms<<'\n';
    }
    std::filesystem::remove_all(path);
}
}
int main() {
    directMetadata();ddaChecks();cubeRegressionAndPerformance();
    std::cout<<"Structure renderer passed: 8 stair images, upper slabs, thin/paired/hinged doors, internal UVs, transparent gaps, leaves, window guards, fullcube day/night\n";
}
