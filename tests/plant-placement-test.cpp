// Regression: plants / flower pot / vines / cocoa (user report: "草 花 盆等放不了，
// 藤蔓生成的方向有问题，可可豆的ui显示有问题").
//  1. Placement of source plant items (tall grass, flowers, mushrooms, dead
//     bush, cactus, reeds, flower pot, vine, cocoa beans, nether wart, web).
//  2. Vine facing: VineTile bitmask (1=S/+Z 2=W/-X 4=N/-Z 8=E/+X) must survive
//     catalog mapping and render against the matching side of the cell, and
//     generated tree vines must face their supporting trunk/leaves.
//  3. Cocoa: facing survives catalog mapping, world geometry is a pod (not a
//     full cube) beside its log, and cocoa beans (351:3) are held as the
//     items.png sprite, not as the cocoa tile.
#include "ActorRenderer.hpp"
#include "BlockCollision.hpp"
#include "ItemCatalog.hpp"
#include "ItemPlacement.hpp"
#include "LegacyCatalog.hpp"
#include "OriginalWorld.hpp"
#include "WorldRenderer.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <map>
#include <string>
#include <tuple>
#include <vector>

#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); std::abort(); } } while (0)

namespace {
using mcworld::PlacementChange;
using mcworld::PlacementContext;
using mcworld::placementItem;
using mcworld::placementState;
using mcworld::planItemPlacement;

struct Grid {
    std::map<std::tuple<int,int,int>,uint16_t> blocks;
    uint16_t get(int x,int y,int z) const {
        auto it=blocks.find({x,y,z}); return it==blocks.end() ? 0 : it->second;
    }
    void put(int x,int y,int z,int tile,int data=0) {
        const int render=placementState(tile,data); CHECK(render>=0);
        blocks[{x,y,z}]=uint16_t(render+1);
    }
    // Hit cell is always (10,100,12).
    PlacementContext context(int face) {
        PlacementContext c; c.hit={true,false,10,100,12,face,get(10,100,12),2};
        c.yaw=0; c.clickHeight=.5f; c.playerX=0; c.playerY=0; c.playerZ=0;
        c.blockAt=[this](int x,int y,int z) { return get(x,y,z); };
        c.available=[](int x,int y,int z) { return x>=0&&x<32&&y>0&&y<256&&z>=0&&z<32; };
        return c;
    }
};
bool place(Grid& g,int id,int aux,int face,std::vector<PlacementChange>& out) {
    const auto* item=placementItem(id,aux); CHECK(item && item->placeable);
    auto context=g.context(face); return planItemPlacement(*item,context,out);
}
void expect(Grid& g,int id,int aux,int face,int x,int y,int z,int tile,int data) {
    std::vector<PlacementChange> out;
    if (!place(g,id,aux,face,out)) { std::fprintf(stderr,"refused item %d:%d face %d\n",id,aux,face); std::abort(); }
    CHECK(out.size()==1);
    CHECK(out[0].x==x && out[0].y==y && out[0].z==z);
    CHECK(out[0].renderIndex==placementState(tile,data));
    CHECK(legacy::kBlockSpecs[out[0].renderIndex].tileId==tile && legacy::kBlockSpecs[out[0].renderIndex].data==data);
}
void refuse(Grid& g,int id,int aux,int face) {
    std::vector<PlacementChange> out{{0,0,0,0}};
    CHECK(!place(g,id,aux,face,out) && out.empty());
}

void plants() {
    Grid g;
    // Bush::mayPlaceOn: grass, dirt, farmland.
    for (int soil : {2,3,60}) {
        g.put(10,100,12,soil);
        for (int aux=0; aux<3; ++aux) expect(g,31,aux,1,10,101,12,31,aux);
        expect(g,37,0,1,10,101,12,37,0); expect(g,38,0,1,10,101,12,38,0);
    }
    g.put(10,100,12,1); refuse(g,37,0,1); refuse(g,31,1,1); refuse(g,32,0,1);
    g.put(10,100,12,12); refuse(g,38,0,1);
    expect(g,32,0,1,10,101,12,32,0); // DeadBushTile: sand only
    // Flowers on the side of a grass block go into the air cell beside it,
    // which has no soil below -> refused (source mayPlace checks y-1).
    g.put(10,100,12,2); refuse(g,37,0,2);
    // Mushrooms: mycelium or a solid cube.
    for (int soil : {110,1,3}) { g.put(10,100,12,soil); expect(g,39,0,1,10,101,12,39,0); expect(g,40,0,1,10,101,12,40,0); }
    g.put(10,100,12,20); refuse(g,39,0,1); // glass is not Tile::solid
    // Replaceable vegetation: planting on a tall-grass cell replaces it in place.
    Grid r; r.put(10,99,12,2); r.put(10,100,12,31,1);
    expect(r,38,0,1,10,100,12,38,0);

    // Flower pot (TilePlanterItem 390 -> tile 140): top-solid below.
    g.put(10,100,12,1); expect(g,390,0,1,10,101,12,140,0);
    g.put(10,100,12,44,0); refuse(g,390,0,1);       // lower slab is not top-solid
    g.put(10,100,12,44,8); expect(g,390,0,1,10,101,12,140,0);
    Grid side; side.put(10,100,12,1); refuse(side,390,0,2); // air below the side cell

    // Cactus: sand/cactus below and no solid material on the four sides.
    Grid c; c.put(10,100,12,12); expect(c,81,0,1,10,101,12,81,0);
    c.put(11,101,12,1); refuse(c,81,0,1);
    c.put(11,101,12,37); expect(c,81,0,1,10,101,12,81,0); // flowers are not solid
    c.put(10,100,12,3); c.blocks.erase({11,101,12}); refuse(c,81,0,1);

    // Reeds (338 -> 83): grass/dirt/sand with adjacent water at y-1, or reed below.
    Grid w; w.put(10,100,12,12); refuse(w,338,0,1);
    w.put(11,100,12,9); expect(w,338,0,1,10,101,12,83,0);
    w.put(10,100,12,1); refuse(w,338,0,1);
    Grid stack; stack.put(10,100,12,83); expect(stack,338,0,1,10,101,12,83,0);

    // Nether wart seeds (372 -> 115): soul sand, top face.
    Grid n; n.put(10,100,12,88); expect(n,372,0,1,10,101,12,115,0);
    refuse(n,372,0,2); n.put(10,100,12,60); refuse(n,372,0,1);

    // Web is a plain TileItem.
    Grid web; web.put(10,100,12,1); expect(web,30,0,2,10,100,11,30,0);
}

void vines() {
    // Clicked face -> VineTile facing bit of the NEW vine cell.
    // face 2 (north of the block): vine at z-1, block is to its SOUTH -> 1.
    struct Case { int face, x, z, data; };
    for (const Case k : {Case{2,10,11,1}, Case{3,10,13,4}, Case{4,9,12,8}, Case{5,11,12,2}}) {
        Grid g; g.put(10,100,12,1);
        expect(g,106,0,k.face,k.x,100,k.z,106,k.data);
        // The emitted state's attached side really is the clicked block.
        const int d=k.data;
        const int sx=k.x+(d==2?-1:d==8?1:0), sz=k.z+(d==1?1:d==4?-1:0);
        CHECK(sx==10 && sz==12);
    }
    Grid g; g.put(10,100,12,1); refuse(g,106,0,1); refuse(g,106,0,0);
    g.put(10,100,12,44,0); refuse(g,106,0,2); // slab is not cube-shaped
    g.put(10,100,12,18,0); expect(g,106,0,3,10,100,13,106,4); // leaves hold vines

    // Catalog: every facing bitmask has its own render state with exact data.
    for (int data=1; data<16; ++data) {
        const uint16_t id=mcworld::OriginalWorld::catalogForTile(106,uint8_t(data));
        const auto& spec=legacy::kBlockSpecs[id-1];
        CHECK(spec.tileId==106 && std::string(spec.shape)=="vine" && spec.data==data);
        CHECK(mcworld::collisionState(id).tileId==106 && mcworld::collisionState(id).data==data);
    }
    // Geometry: one plane per bit, on the SAME side the bit names.
    for (int data=1; data<16; ++data) {
        const auto& spec=legacy::kBlockSpecs[mcworld::OriginalWorld::catalogForTile(106,uint8_t(data))-1];
        const auto geometry=worldrender::buildGeometry(spec);
        int bits=0; for (int b=0;b<4;++b) bits+=(data>>b)&1;
        CHECK(int(geometry.planes.size())==bits && geometry.boxes.empty());
        auto hits=[&](worldrender::Point origin, worldrender::Point dir) {
            worldrender::Hit hit; return worldrender::rayIntersection(geometry,origin,dir,0,2,hit) && hit.distance<.1f;
        };
        // Rays from just inside each face toward that face.
        CHECK(hits({.5f,.5f,.9f},{0,0,1}) == bool(data&1));   // south (+Z)
        CHECK(hits({.1f,.5f,.5f},{-1,0,0}) == bool(data&2));  // west (-X)
        CHECK(hits({.5f,.5f,.1f},{0,0,-1}) == bool(data&4));  // north (-Z)
        CHECK(hits({.9f,.5f,.5f},{1,0,0}) == bool(data&8));   // east (+X)
    }
}

void generatedVines() {
    // Decorated world: generated vine bits must point at what the source hung
    // them from (TreeFeature/SwampTree/MegaTree set the bit facing the trunk or
    // leaves; lower vines continue the bit of the vine above). A few vines are
    // later overwritten around by a neighbouring tree's leaves (vine material
    // is not solid), so require a large majority, and require the MIRRORED
    // axis convention to fail - that is what a swapped layer would produce.
    mcworld::OriginalWorld world(12345,864,false,true);
    int vines=0, attached=0, mirrored=0;
    static constexpr int dx[4]={0,-1,0,1}, dz[4]={1,0,-1,0};
    const auto supported=[&](int x,int y,int z,int data,bool mirror) {
        for (int b=0;b<4;++b) if (data&(1<<b)) {
            const int s=mirror?-1:1;
            const int n=mcworld::collisionState(world.blockAt(x+s*dx[b],y,z+s*dz[b])).tileId;
            if (!(mcworld::collisionVineAttachable(n) || n==17 || n==18)) return false;
        }
        return true;
    };
    for (int cx=0; cx<54 && vines<200; ++cx) for (int cz=0; cz<54 && vines<200; ++cz) {
        if ((cx+cz)%4) continue;
        for (int x=cx*16+1; x<cx*16+15; ++x) for (int z=cz*16+1; z<cz*16+15; ++z)
            for (int y=50; y<128; ++y) {
                const auto state=mcworld::collisionState(world.blockAt(x,y,z));
                if (state.tileId!=106 || state.data==0) continue;
                ++vines;
                const auto above=mcworld::collisionState(world.blockAt(x,y+1,z));
                const bool hanging=above.tileId==106 && (above.data & state.data);
                attached += supported(x,y,z,state.data,false) || hanging;
                mirrored += supported(x,y,z,state.data,true);
            }
    }
    std::fprintf(stderr,"generated vines: %d, facing support: %d, mirrored-axis support: %d\n",vines,attached,mirrored);
    CHECK(vines >= 50);
    CHECK(attached*100 >= vines*98);
    CHECK(mirrored*2 < vines);
}

void cocoa() {
    // DyePowderItem BROWN: side of a jungle log only, data = Direction to log.
    struct Case { int face, x, z, data; };
    for (const Case k : {Case{2,10,11,0}, Case{3,10,13,2}, Case{4,9,12,3}, Case{5,11,12,1}}) {
        Grid g; g.put(10,100,12,17,3);
        expect(g,351,3,k.face,k.x,100,k.z,127,k.data);
        static constexpr int sx[4]={0,-1,0,1}, sz[4]={1,0,-1,0}; // Direction::STEP_X/Z
        CHECK(k.x+sx[k.data]==10 && k.z+sz[k.data]==12); // CocoaTile::canSurvive target
    }
    Grid g; g.put(10,100,12,17,3); refuse(g,351,3,1); refuse(g,351,3,0);
    g.put(10,100,12,17,0); refuse(g,351,3,2); // oak log
    g.put(10,100,12,1); refuse(g,351,3,2);

    for (int data=0; data<12; ++data) {
        const uint16_t id=mcworld::OriginalWorld::catalogForTile(127,uint8_t(data));
        const auto& spec=legacy::kBlockSpecs[id-1];
        CHECK(spec.tileId==127 && spec.data==data && std::string(spec.shape)=="cocoa");
        const auto geometry=worldrender::buildGeometry(spec);
        CHECK(!geometry.fullCube && geometry.boxes.empty() && geometry.planes.size()==7);
        // Pod touches the side of the cell facing its log (15/16 or 1/16).
        const int dir=data&3;
        float lo=1, hi=0;
        for (const auto& p : geometry.planes) {
            if (p.face>1) continue; // top/bottom quads span the pod footprint
            for (float a : {0.0f,1.0f}) for (float b : {0.0f,1.0f}) {
                const float x=p.origin.x+p.edgeU.x*a+p.edgeV.x*b, z=p.origin.z+p.edgeU.z*a+p.edgeV.z*b;
                const float v=(dir==1||dir==3)?x:z;
                lo=std::min(lo,v); hi=std::max(hi,v);
            }
        }
        if (dir==0 || dir==3) CHECK(std::abs(hi-15/16.0f)<1e-4f); // log at +Z / +X
        else CHECK(std::abs(lo-1/16.0f)<1e-4f);                  // log at -X / -Z
    }
    // Flower pot is a 6/16 pot, not a full cube.
    const auto pot=worldrender::buildGeometry(legacy::kBlockSpecs[mcworld::OriginalWorld::catalogForTile(140,0)-1]);
    CHECK(!pot.fullCube && pot.boxes.size()==1 && std::abs(pot.boxes[0].high.y-6/16.0f)<1e-6f);

    // Held: cocoa beans / flower pot / reeds are independent Items with
    // items.png sprites; a TileItem flower is not.
    CHECK(mcworld::placementItem(351,3)->renderIndex>=0); // still placeable
    CHECK(hasHeldItemSprite(351,3) && hasHeldItemSprite(390,0) && hasHeldItemSprite(338,0) && hasHeldItemSprite(372,0));
    CHECK(!hasHeldItemSprite(37,0) && !hasHeldItemSprite(1,0) && !hasHeldItemSprite(351,0));
    std::vector<uint32_t> before(320*200,0xff000000u), after=before;
    renderHeldItemOverlay(after.data(),320,200,351,3,0,1);
    CHECK(after!=before);
}
} // namespace

int main() {
    plants(); vines(); cocoa(); generatedVines();
    std::cout << "plant-placement-test: plants/pot/reeds/cactus/wart/web placement, vine facing bits, "
                 "generated vine attachment, cocoa facing/geometry and held sprite passed\n";
}
