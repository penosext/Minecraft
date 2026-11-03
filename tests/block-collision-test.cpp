// Targeted host regression; no SDK, renderer, full repository build or network.
// Link World/OriginalWorld/ItemPlacement/BlockCollision + original terrain modules.
#define main collisionFixtureHelpersMain
#include "world-test.cpp"
#undef main
#include "BlockCollision.hpp"
#include "ItemCatalog.hpp"
#include "ItemPlacement.hpp"
#include "OriginalWorld.hpp"
#include <map>
#include <tuple>

using mcworld::CollisionBox;
using mcworld::CollisionState;
using mcworld::blockCollisionBoxes;
using mcworld::collisionIntersects;
uint16_t stateId(int tile, int data=0) {
    const int index=mcworld::placementState(tile,data);
    assert(index>=0); return uint16_t(index+1);
}
bool intersects(const mcworld::CollisionBoxes& boxes,const CollisionBox& b) {
    for (int i=0;i<boxes.count;++i) if (collisionIntersects(boxes.boxes[size_t(i)],b)) return true;
    return false;
}
// The item catalog still pins the original 312 render states, so tiles added in
// this pass have no placementState id. Neighbor-dependent shapes (wall, pane,
// stairs) are therefore exercised through a context that returns tile/data
// directly, bypassing the render-id indirection entirely.
struct DirectNeighbors {
    std::map<std::tuple<int,int,int>,CollisionState> cells;
    void set(int x,int z,int tile,int data=0) { cells[{x,0,z}]={tile,data}; }
    void clear() { cells.clear(); }
    mcworld::CollisionContext context() const {
        mcworld::CollisionContext c;
        c.available=[](int,int,int) { return true; };
        c.stateAt=[this](int x,int y,int z) {
            const auto i=cells.find({x,y,z});
            return i==cells.end()?CollisionState{0,0}:i->second;
        };
        return c;
    }
};
void sourceBoxes() {
    const CollisionBox center {.25f,0,.25f,.75f,1,.75f};
    for (int tile : {8,9,10,11,30,31,32,37,38,39,40,50,51,55,59,66,69,70,72,77,83,106,115,131,132,141,142})
        assert(blockCollisionBoxes({tile,0},0,0,0).count==0);
    assert(intersects(blockCollisionBoxes({2,0},0,0,0),center)); // grass block != grass plant
    auto unknown=blockCollisionBoxes({-1,-1},0,0,0);
    assert(unknown.conservative && unknown.count==1 && intersects(unknown,center));
    assert(mcworld::collisionState(65535).tileId==-1);
    assert(blockCollisionBoxes({65,0},0,0,0).conservative); // not a valid source state; never guess old ladder orientation
    assert(!collisionIntersects({0,0,0,1,1,1},{1,0,0,2,1,1})); // touching != intersection
    assert(blockCollisionBoxes({44,0},0,0,0).boxes[0].y1==.5f);
    assert(blockCollisionBoxes({44,8},0,0,0).boxes[0].y0==.5f);
    assert(!intersects(blockCollisionBoxes({44,0},0,0,0),{0,.5f,0,1,1,1}));
    assert(blockCollisionBoxes({78,0},0,0,0).count==0);
    assert(blockCollisionBoxes({78,7},0,0,0).boxes[0].y1==.875f);
    assert(blockCollisionBoxes({60,0},0,0,0).boxes[0].y1==1); // source farmland collision FULL
    assert(blockCollisionBoxes({88,0},0,0,0).boxes[0].y1==.875f);
    auto cactus=blockCollisionBoxes({81,0},0,0,0).boxes[0];
    assert(cactus.x0==.0625f && cactus.z1==.9375f && cactus.y1==.9375f);
    assert(blockCollisionBoxes({54,0},0,0,0).boxes[0].y1==.875f);
    assert(blockCollisionBoxes({140,0},0,0,0).boxes[0].y1==.375f);
    assert(blockCollisionBoxes({111,0},0,0,0).boxes[0].y1==.015625f);
    assert(blockCollisionBoxes({118,0},0,0,0).count==5);
    assert(!intersects(blockCollisionBoxes({118,0},0,0,0),{.25f,.5f,.25f,.75f,1,.75f}));
    assert(blockCollisionBoxes({120,4},0,0,0).count==2);
    for (int dir=2;dir<=5;++dir) {
        const auto ladder=blockCollisionBoxes({65,dir},0,0,0);
        assert(ladder.count==1 && !ladder.conservative && !intersects(ladder,center));
        assert(mcworld::collisionState(stateId(65,dir)).data==dir);
    }
    // Neighbor fixtures use actual source state -> current catalog IDs, never
    // render mesh names. Fence/Panes have intentionally different attachment.
    std::map<std::tuple<int,int,int>,uint16_t> grid;
    mcworld::CollisionContext context {
        [&](int x,int y,int z) { const auto i=grid.find({x,y,z}); return i==grid.end()?uint16_t(0):i->second; },
        [](int,int,int) { return true; }
    };
    auto fence=blockCollisionBoxes({85,0},0,0,0,context);
    assert(fence.count==1 && fence.boxes[0].y1==1.5f);
    assert(intersects(fence,{.4f,1.25f,.4f,.6f,1.45f,.6f}));
    for (int noAttach : {18,20,46,86,89}) {
        grid[{-1,0,0}]=stateId(noAttach);
        assert(blockCollisionBoxes({85,0},0,0,0,context).boxes[0].x0==.375f);
    }
    grid[{-1,0,0}]=stateId(1);
    assert(blockCollisionBoxes({85,0},0,0,0,context).boxes[0].x0==0);
    for (int attach : {20,46,86,89}) {
        grid.clear(); grid[{-1,0,0}]=stateId(attach);
        const auto pane=blockCollisionBoxes({102,0},0,0,0,context);
        assert(pane.count==1 && pane.boxes[0].x0==0 && pane.boxes[0].x1==.5f);
    }
    grid.clear(); grid[{1,0,0}]=stateId(54);
    assert(blockCollisionBoxes({54,0},0,0,0,context).boxes[0].x1==1);
    // Door lower/upper composite states: closed thin face; open swings to side,
    // right hinge swings to opposite side; upper follows lower open/facing.
    for (int tile : {64,71}) for (int dir=0;dir<4;++dir) for (int hinge=0;hinge<2;++hinge) for (int open=0;open<2;++open) {
        const int lower=dir|(open?4:0), upper=8|hinge;
        grid.clear(); grid[{0,0,0}]=stateId(tile,lower); grid[{0,1,0}]=stateId(tile,upper);
        const auto a=blockCollisionBoxes({tile,lower},0,0,0,context).boxes[0];
        const auto b=blockCollisionBoxes({tile,upper},0,1,0,context).boxes[0];
        assert(a.x0==b.x0 && a.z0==b.z0 && a.x1==b.x1 && a.z1==b.z1);
        assert((a.x1-a.x0==.1875f) != (a.z1-a.z0==.1875f));
        if (dir==0 && !open) assert(a.x0==0 && a.x1==.1875f);
        if (dir==0 && open && !hinge) assert(a.z0==0 && a.z1==.1875f);
        if (dir==0 && open && hinge) assert(a.z0==.8125f && a.z1==1);
    }
    // All 8 stair states, plus neighbor-dependent inner/outer corners and lock.
    grid.clear();
    for (int data=0;data<8;++data) {
        const auto stair=blockCollisionBoxes({53,data},0,0,0,context);
        assert(stair.count==2 && !stair.conservative);
        assert(stair.boxes[0].y0==((data&4)? .5f:0));
    }
    grid[{1,0,0}]=stateId(67,3); // east stair backed by north -> outer quarter
    auto outer=blockCollisionBoxes({53,0},0,0,0,context);
    assert(outer.count==2 && outer.boxes[1].z1==.5f);
    grid[{0,0,1}]=stateId(53,0); // lock prevents outer cut
    assert(blockCollisionBoxes({53,0},0,0,0,context).boxes[1].z1==1);
    grid.clear(); grid[{-1,0,0}]=stateId(67,3);
    assert(blockCollisionBoxes({53,0},0,0,0,context).count==3);
    for (int i=0;i<legacy::kBlockCount;++i) {
        const auto boxes=blockCollisionBoxes(mcworld::collisionState(uint16_t(i+1)),0,0,0);
        assert(boxes.count>=0 && boxes.count<=5);
    }
}
// Static blocks completed in this pass. These assert the collider geometry
// directly from (tileId,data) because the item catalog still pins the original
// 312 render states, so these tiles have no placementState id yet.
// independent implementation, behaviour verified against observed game rules.
void staticBlockBoxes() {
    // A body standing in the cell above, and the same body sunk into the cell.
    const CollisionBox standing {.3f,1.0f,.3f,.7f,2.8f,.7f};
    const CollisionBox sunk {.3f,.01f,.3f,.7f,1.8f,.7f};
    const CollisionBox center {.25f,0,.25f,.75f,1,.75f};
    DirectNeighbors grid;
    const auto context=grid.context();
    // --- full-cell cubes: stand on top, blocked from the side, nothing above ---
    for (int tile : {19,22,41,42,45,57,95,122,123,124,133,152,155,159,170,172,173}) {
        for (int data : {0,1,15}) {
            const auto boxes=blockCollisionBoxes({tile,data},0,0,0);
            assert(boxes.count==1);
            const auto& b=boxes.boxes[0];
            assert(b.x0==0 && b.y0==0 && b.z0==0 && b.x1==1 && b.y1==1 && b.z1==1);
            assert(intersects(boxes,center));          // side contact blocks
            assert(intersects(boxes,sunk));            // body cannot sink in
            assert(!intersects(boxes,standing));       // stands on top, head free
            assert(!boxes.conservative);               // a known, audited shape
        }
    }
    // --- carpet: 1/16 sheet. Standing on it must not intersect the body, and
    // a jump must clear it; only a body below 1/16 touches it.
    for (int data=0;data<16;++data) {
        const auto carpet=blockCollisionBoxes({171,data},0,0,0);
        assert(carpet.count==1 && !carpet.conservative);
        const auto& b=carpet.boxes[0];
        assert(b.y0==0 && b.y1==.0625f && b.x0==0 && b.x1==1 && b.z0==0 && b.z1==1);
        assert(!intersects(carpet,standing));
        assert(intersects(carpet,{.3f,0,.3f,.7f,.05f,.7f}));     // feet inside the sheet
        assert(!intersects(carpet,{.3f,.0625f,.3f,.7f,1,.7f}));  // resting exactly on top
        assert(!intersects(carpet,{.3f,.2f,.3f,.7f,1.9f,.7f}));  // mid-jump clears it
    }
    // --- enchantment table: 3/4 height, full footprint ---
    const auto table=blockCollisionBoxes({116,0},0,0,0);
    assert(table.count==1 && !table.conservative);
    assert(table.boxes[0].y1==.75f);
    assert(table.boxes[0].x0==0 && table.boxes[0].x1==1);
    assert(intersects(table,{.3f,.5f,.3f,.7f,.7f,.7f}));         // side contact below 3/4
    assert(!intersects(table,{.3f,.75f,.3f,.7f,2.5f,.7f}));      // stands on the 3/4 top
    assert(!intersects(table,{.3f,.8f,.3f,.7f,1.9f,.7f}));       // jump clears it
    // --- anvil: full height, narrowed 2/16 on the facing axis ---
    for (int damage=0;damage<4;++damage) for (int dir=0;dir<4;++dir) {
        const int data=(damage<<2)|dir;
        const auto anvil=blockCollisionBoxes({145,data},0,0,0);
        assert(anvil.count==1 && !anvil.conservative);
        const auto& b=anvil.boxes[0];
        assert(b.y0==0 && b.y1==1);                   // full height regardless of damage
        if (dir==1 || dir==3) {                       // facing west/east: Z is narrowed
            assert(b.x0==0 && b.x1==1 && b.z0==.125f && b.z1==.875f);
            assert(!intersects(anvil,{.3f,0,0,.7f,1,.1f}));      // clearance on the narrow side
        } else {                                      // facing south/north: X is narrowed
            assert(b.x0==.125f && b.x1==.875f && b.z0==0 && b.z1==1);
            assert(!intersects(anvil,{0,0,.3f,.1f,1,.7f}));
        }
        assert(intersects(anvil,center));             // centre is always blocked
        assert(intersects(anvil,sunk));
        assert(!intersects(anvil,standing));          // stands on top
        // Damage bits must not alter the collider at all.
        const auto plain=blockCollisionBoxes({145,dir},0,0,0).boxes[0];
        assert(b.x0==plain.x0 && b.x1==plain.x1 && b.z0==plain.z0 && b.z1==plain.z1);
    }
    // --- cobblestone wall: 1.5 tall post, extends on connected sides ---
    grid.clear();
    for (int data : {0,1}) {
        const auto post=blockCollisionBoxes({139,data},0,0,0,context);
        assert(post.count==1 && !post.conservative);
        const auto& b=post.boxes[0];
        assert(b.y1==1.5f);                                   // taller than one cell
        assert(b.x0==.3125f && b.x1==.6875f && b.z0==.3125f && b.z1==.6875f);
        assert(intersects(post,{.4f,1.25f,.4f,.6f,1.45f,.6f})); // body above the cell is caught
        assert(!intersects(post,{0,0,0,.25f,1.5f,1}));          // corners stay open
        assert(!intersects(post,{.3f,1.5f,.3f,.7f,2.5f,.7f}));  // standing on the wall top
    }
    // A solid cube neighbour and another wall both extend the arm to the edge.
    for (int neighbour : {1,139,170}) {
        grid.clear(); grid.set(-1,0,neighbour);
        const auto west=blockCollisionBoxes({139,0},0,0,0,context);
        assert(west.count==1 && west.boxes[0].x0==0 && west.boxes[0].y1==1.5f);
    }
    // Glass does not attach: the post keeps its 4/16 half-width.
    grid.clear(); grid.set(-1,0,20);
    assert(blockCollisionBoxes({139,0},0,0,0,context).boxes[0].x0==.3125f);
    // Opposite sides connected -> a single box spanning the full axis.
    grid.clear(); grid.set(-1,0,1); grid.set(1,0,1);
    const auto through=blockCollisionBoxes({139,0},0,0,0,context);
    assert(through.count==1 && through.boxes[0].x0==0 && through.boxes[0].x1==1);
    assert(through.boxes[0].z0==.3125f && through.boxes[0].z1==.6875f);
    // --- stained glass pane (160): reuses the audited thin-fence collider ---
    grid.clear();
    const auto alone=blockCollisionBoxes({160,0},0,0,0,context);
    assert(alone.count==2 && !alone.conservative);
    assert(!intersects(alone,{0,0,0,.4f,1,.4f}));                // corner is open
    assert(intersects(alone,{.4f,0,.4f,.6f,1,.6f}));             // crossing centre blocks
    assert(!intersects(alone,{.3f,1,.3f,.7f,2.5f,.7f}));         // stands on top
    for (int attach : {20,95,102,160,1}) {
        grid.clear(); grid.set(-1,0,attach);
        const auto pane=blockCollisionBoxes({160,0},0,0,0,context);
        bool west=false;
        for (int i=0;i<pane.count;++i)
            if (pane.boxes[size_t(i)].x0==0 && pane.boxes[size_t(i)].x1==.5f) west=true;
        assert(west); // attaches to glass, both pane tiles and solid cubes
    }
    // --- the four new stair types share the audited stair collider ---
    for (int tile : {108,135,136,156}) {
        grid.clear();
        for (int data=0;data<8;++data) {
            const auto stair=blockCollisionBoxes({tile,data},0,0,0,context);
            assert(stair.count==2 && !stair.conservative);
            assert(stair.boxes[0].y0==((data&4)? .5f:0));
            assert(stair.boxes[0].y1==((data&4)? 1:.5f));
            assert(intersects(stair,sunk));          // the solid half always blocks
            assert(!intersects(stair,standing));     // stands on the step
        }
        // The low step leaves the opposite quarter open to walk/step onto.
        const auto east=blockCollisionBoxes({tile,0},0,0,0,context);
        assert(!intersects(east,{.05f,.5f,.3f,.45f,1,.7f}));
        // Corner cut against a differently-facing stair of another new type.
        grid.clear(); grid.set(1,0,67,3);
        const auto outer=blockCollisionBoxes({tile,0},0,0,0,context);
        assert(outer.count==2 && outer.boxes[1].z1==.5f);
        grid.set(0,1,tile,0); // lock prevents the outer cut
        assert(blockCollisionBoxes({tile,0},0,0,0,context).boxes[1].z1==1);
    }
    // Quartz pillar/chiselled metadata never changes the full-cell collider.
    for (int data=0;data<5;++data) {
        const auto quartz=blockCollisionBoxes({155,data},0,0,0);
        assert(quartz.count==1 && quartz.boxes[0].y1==1 && !quartz.conservative);
    }
    std::cout<<"static blocks: 26 tiles - cubes, carpet 1/16, wall 1.5 post, enchant 3/4, "
               "anvil per facing, 4 stair types, stained pane OK\n";
}
struct Fixture {
    Temp root;
    std::string path;
    Bytes base;
    Fixture() {
        World world; world.create(654321,root.path,false); path=world.path();
        base=read(path+"/chunks.bin"); std::fill(base.begin()+40,base.end(),0);
        for (int z=0;z<mcworld::Depth;++z) for (int x=0;x<mcworld::Width;++x) block(base,x,1,z,stateId(1));
    }
    World open(Bytes chunks,float x,float y,float z,float velocity=0) {
        synchronize(path,std::move(chunks)); player(path,x,y,z,velocity);
        World world; world.open(path,root.path); return world;
    }
};
void advance(World& world,int count,float yaw=90,float forward=1) {
    for (int i=0;i<count;++i) world.update(.05f,forward,0,false,yaw);
}
void worldCollision() {
    Fixture f;
    // Traversal and save/open while inside each nonblocking source state. Ladder
    // is parallel to travel so its real 2/16 backing remains out of the body.
    for (const CollisionState state : {CollisionState{31,1},{50,0},{65,2},{8,0},{9,0},{10,0},{11,0}}) {
        auto chunks=f.base;
        for (int y=2;y<=3;++y) block(chunks,11,y,10,stateId(state.tileId,state.data));
        auto world=f.open(chunks,10,2,10.5f);
        advance(world,7); assert(world.playerX()>11.3f && world.playerX()<11.7f);
        world.save(); World reopened; reopened.open(f.path,f.root.path);
        near(reopened.playerX(),world.playerX()); advance(reopened,10);
        assert(reopened.playerX()>13);
    }
    auto chunks=f.base;
    for (int y=2;y<7;++y) block(chunks,11,y,10,stateId(2));
    auto wall=f.open(chunks,10,2,10.5f); advance(wall,30);
    near(wall.playerX(),10.75f); near(wall.playerY(),2);
    chunks=f.base; block(chunks,11,2,10,stateId(44,0));
    auto slab=f.open(chunks,11.5f,2.5f,10.5f); advance(slab,5,0,0);
    near(slab.playerY(),2.5f); slab.save(); World slabOpen; slabOpen.open(f.path,f.root.path);
    near(slabOpen.playerY(),2.5f);
    auto climbSlab=f.open(chunks,10.5f,2,10.5f); advance(climbSlab,4);
    near(climbSlab.playerY(),2.5f);
    // High-speed fall lands on the source slab top, never a full-cell roof.
    auto falling=f.open(chunks,11.5f,7,10.5f,-35); advance(falling,8,0,0);
    near(falling.playerY(),2.5f);
    // Closed/open doors swing the thin collider; test movement along X. Close
    // has X face at west, open lies along Z and permits the center path.
    for (bool open : {false,true}) {
        chunks=f.base; block(chunks,11,2,10,stateId(64,open?4:0)); block(chunks,11,3,10,stateId(64,8));
        block(chunks,10,4,10,stateId(1)); block(chunks,11,4,10,stateId(1)); // prevent one-block auto-step
        auto door=f.open(chunks,10,2,10.5f); advance(door,15);
        if (open) assert(door.playerX()>12); else near(door.playerX(),10.75f);
    }
    // Player feet are above fence's owning cell: scan y-1 catches its 1.5 AABB.
    chunks=f.base; block(chunks,11,2,10,stateId(85));
    auto fenceTop=f.open(chunks,11.5f,3.5f,10.5f); advance(fenceTop,3,0,0);
    near(fenceTop.playerY(),3.5f);
    rejects([&] { auto bad=f.open(chunks,11.5f,3.25f,10.5f); (void)bad; });
    // Four valid source orientations stop at the thin backing, never at cell
    // entry. Horizontal collision on ladder causes climb without jump input.
    for (int dir=2;dir<=5;++dir) {
        chunks=f.base;
        for (int y=2;y<=9;++y) block(chunks,11,y,10,stateId(65,dir));
        const float x=dir==4?11.5f:dir==5?11.5f:11.5f;
        const float yaw=dir==2?0:dir==3?180:dir==4?90:270;
        auto ladder=f.open(chunks,x,2,10.5f); advance(ladder,12,yaw);
        assert(ladder.playerY()>2.5f);
        if (dir==4) near(ladder.playerX(),11.625f);
        if (dir==5) near(ladder.playerX(),11.375f);
        if (dir==2) near(ladder.playerZ(),10.625f);
        if (dir==3) near(ladder.playerZ(),10.375f);
    }
    chunks=f.base;
    for (int y=2;y<12;++y) block(chunks,11,y,10,stateId(65,4));
    auto ladderFall=f.open(chunks,11.5f,8,10.5f,-35); advance(ladderFall,2,0,0);
    assert(ladderFall.playerY()>=7.699f); // .1s at min fall -3/s, not -35/s
    chunks=f.base;
    auto boundary=f.open(chunks,.25f,2,10.5f); advance(boundary,10,270);
    near(boundary.playerX(),.25f);
    std::cout<<"block collision: source boxes, traversal/save-open, slab/door/fence, ladder adaptation and finite boundary OK\n";
}
// Climb/step behaviour. Every number below was measured against this build and
// cross-checked with the audited source rules named in each comment; they are an
// independent implementation, behaviour verified against observed game rules.
// Conversions: the source clamps ladder fall to .15/tick and climbs at .2/tick,
// which this port adapts at 20Hz to 3 and 4 blocks per second respectively.
void climbing() {
    Fixture f;
    // --- vines have NO collider of their own: the body shares their cells ---
    assert(blockCollisionBoxes({106,15},0,0,0).count==0);
    for (int data=0;data<16;++data) assert(blockCollisionBoxes({106,data},0,0,0).count==0);
    // --- climbable predicate: ladders by id, vines only when supported ---
    DirectNeighbors grid;
    const auto context=grid.context();
    for (int dir=2;dir<=5;++dir) assert(mcworld::collisionClimbable({65,dir},0,0,0,context));
    grid.clear();
    assert(!mcworld::collisionClimbable({106,15},0,0,0,context)); // nothing holds it
    assert(!mcworld::collisionClimbable({0,0},0,0,0,context));
    assert(!mcworld::collisionClimbable({1,0},0,0,0,context));    // plain stone is not a ladder
    // Each of the four side faces, and a bare roof, supports a vine on its own.
    for (const auto offset : {std::pair<int,int>{-1,0},{1,0},{0,-1},{0,1}}) {
        grid.clear(); grid.set(offset.first,offset.second,1);
        assert(mcworld::collisionClimbable({106,15},0,0,0,grid.context()));
    }
    {
        mcworld::CollisionContext roof;
        roof.available=[](int,int,int) { return true; };
        roof.stateAt=[](int,int y,int) { return y==1?CollisionState{1,0}:CollisionState{0,0}; };
        assert(mcworld::collisionClimbable({106,15},0,0,0,roof)); // hanging from a roof
    }
    // Source VineTile::isAcceptableNeighbor wants cube-shaped AND motion-blocking.
    // That is wider than fence attachment: glass/leaves/ice/TNT/pumpkin are never
    // buildable or are vegetable/explosive, so they hold a vine but take no fence.
    for (int tile : {1,2,5,20,18,46,79,86,89,95,103,123,124}) {
        grid.clear(); grid.set(1,0,tile);
        assert(mcworld::collisionClimbable({106,15},0,0,0,grid.context()));
    }
    // Non-cube shapes are rejected on both paths: slabs, every stair, snow layer,
    // farmland, cactus, chest, panes, fences/walls, carpet, ladder and vine.
    for (int tile : {44,53,67,108,78,60,81,54,101,102,85,113,139,171,116,145,65,106}) {
        grid.clear(); grid.set(1,0,tile);
        assert(!mcworld::collisionClimbable({106,15},0,0,0,grid.context()));
    }
    // --- vine climb up, hold, descend, leave-wall, unsupported ---
    auto vineWall=[&](bool support) {
        auto chunks=f.base;
        for (int y=2;y<=20;++y) {
            if (support) block(chunks,11,y,10,stateId(1));
            block(chunks,10,y,10,stateId(106,15));
        }
        return chunks;
    };
    auto climb=f.open(vineWall(true),10.5f,4,10.5f);
    advance(climb,20,90); // yaw 90 drives +x into the wall face at x=11
    near(climb.playerY(),7.608f,.05f); // 1.0s of climb; clamped near 4 blocks/s
    assert(climb.playerY()>7.5f);
    // Pressing into the wall must not shove the body out of the vine column: the
    // stone face stops it at 11 - Radius, still inside cell x=10, so onLadder()
    // keeps reading the vine and the climb continues.
    near(climb.playerX(),10.75f);
    near(climb.playerZ(),10.5f);
    // Released keys on a vine fall at the clamped rate, not free-fall, and the
    // saved -35 velocity does not punch through it.
    auto hold=f.open(vineWall(true),10.5f,18,10.5f,-35);
    advance(hold,20,90,0); near(hold.playerY(),15,.05f); // 3 blocks in 1.0s
    auto brief=f.open(vineWall(true),10.5f,18,10.5f,-35);
    advance(brief,2,90,0); assert(brief.playerY()>=17.699f); // .1s at -3/s
    // Walking away from the wall leaves the vine and resumes real gravity.
    auto leave=f.open(vineWall(true),10.5f,6,10.5f);
    advance(leave,20,270); near(leave.playerY(),2); assert(leave.playerX()<9);
    // A vine with no attachment at all is not climbable, so the body just falls.
    auto loose=f.open(vineWall(false),10.5f,6,10.5f);
    advance(loose,20,90,0); near(loose.playerY(),2);
    // --- ladder: hold at rest, climb on contact, release, exit over the top ---
    auto ladderWall=[&](int top) {
        auto chunks=f.base;
        for (int y=2;y<=top;++y) block(chunks,11,y,10,stateId(65,4));
        return chunks;
    };
    // Released on a ladder from rest: the first sub-step still applies gravity
    // before the clamp bites, so 1.0s covers slightly less than the 3-block
    // clamped descent rather than exactly 3.
    auto rest=f.open(ladderWall(20),11.5f,8,10.5f);
    advance(rest,20,90,0); near(rest.playerY(),5.21f,.05f);
    assert(rest.playerY()>8-3.3f); // never free-fall
    auto up=f.open(ladderWall(20),11.5f,4,10.5f);
    advance(up,20,90); near(up.playerY(),7.82f,.05f);   // clamped climb, ~4/s
    near(up.playerX(),11.625f); // held at the 2/16 backing, never pushed away
    // Releasing forward mid-ladder stops the climb and descends at the clamp.
    auto letGo=f.open(ladderWall(20),11.5f,4,10.5f);
    advance(letGo,10,90); const float reached=letGo.playerY();
    advance(letGo,10,90,0); assert(letGo.playerY()<reached && letGo.playerY()>reached-1.6f);
    // Past the top the ladder stops supporting: the body falls back to the floor.
    auto over=f.open(ladderWall(5),11.5f,2,10.5f);
    advance(over,60,90); near(over.playerY(),2); assert(over.playerX()>13);
    std::cout<<"climbing: vine attachment/up/down/leave-wall/unsupported, ladder hold/climb/release/top OK\n";
}
void stairStepUp() {
    Fixture f;
    // Composite halves: the solid half plus the step, for all eight states.
    for (int tile : {53,67,108,109,114,128,134,135,136,156})
        for (int data=0;data<8;++data) {
            const auto boxes=blockCollisionBoxes({tile,data},0,0,0);
            assert(boxes.count==2 && !boxes.conservative);
            const bool upper=(data&4)!=0;
            // setBaseShape: upside-down stairs fill the TOP half, others the bottom.
            assert(boxes.boxes[0].y0==(upper?.5f:0) && boxes.boxes[0].y1==(upper?1:.5f));
            // setStepShape puts the quarter-step in the opposite half.
            assert(boxes.boxes[1].y0==(upper?0:.5f) && boxes.boxes[1].y1==(upper?.5f:1));
            // The step covers exactly half the footprint along the facing axis.
            const auto& step=boxes.boxes[1];
            const float wide=step.x1-step.x0, deep=step.z1-step.z0;
            assert((wide==.5f && deep==1) || (wide==1 && deep==.5f));
        }
    // Facing: 0 east, 1 west, 2 south, 3 north (StairTile DIR_* constants).
    assert(blockCollisionBoxes({53,0},0,0,0).boxes[1].x0==.5f);
    assert(blockCollisionBoxes({53,1},0,0,0).boxes[1].x1==.5f);
    assert(blockCollisionBoxes({53,2},0,0,0).boxes[1].z0==.5f);
    assert(blockCollisionBoxes({53,3},0,0,0).boxes[1].z1==.5f);
    // Adjacent merging: a differently-facing stair behind trims the step to an
    // outer quarter, and an identical stair in the lock cell cancels that trim.
    DirectNeighbors grid;
    grid.set(1,0,67,3);
    auto outer=blockCollisionBoxes({53,0},0,0,0,grid.context());
    assert(outer.count==2 && outer.boxes[1].z1==.5f);
    grid.set(0,1,53,0);
    assert(blockCollisionBoxes({53,0},0,0,0,grid.context()).boxes[1].z1==1);
    // A stair in FRONT adds the inner-corner piece, giving three boxes.
    grid.clear(); grid.set(-1,0,67,3);
    assert(blockCollisionBoxes({53,0},0,0,0,grid.context()).count==3);
    // Upper and lower halves never merge across the upside-down bit.
    grid.clear(); grid.set(1,0,53,3|4);
    assert(blockCollisionBoxes({53,0},0,0,0,grid.context()).boxes[1].z1==1);
    // --- step up without jumping, from every facing's low side ---
    struct Approach { int dir; float x,z,yaw; };
    for (const Approach a : {Approach{0,10.5f,10.5f,90},{1,12.5f,10.5f,270},
                             {2,11.5f,9.5f,0},{3,11.5f,11.5f,180}}) {
        auto chunks=f.base; block(chunks,11,2,10,stateId(53,a.dir));
        auto world=f.open(chunks,a.x,2,a.z);
        advance(world,20,a.yaw);
        // Clears the stair entirely and lands back on the floor beyond it.
        near(world.playerY(),2);
        if (a.dir==0) assert(world.playerX()>13);
        if (a.dir==1) assert(world.playerX()<10);
        if (a.dir==2) assert(world.playerZ()>12);
        if (a.dir==3) assert(world.playerZ()<9);
    }
    // A stair against a wall is climbed in two halves and ends on top, with no
    // jump input at any point.
    auto chunks=f.base;
    block(chunks,11,2,10,stateId(53,0)); block(chunks,12,2,10,stateId(1));
    auto onto=f.open(chunks,10.5f,2,10.5f);
    advance(onto,8,90); near(onto.playerY(),3); assert(onto.playerX()>11.5f);
    // Half slabs settle at their real half height, and a full cube is stepped
    // onto as well: the contact solver rests the body at surface - Epsilon, so
    // the lift sweep has to allow slightly more than a whole block.
    chunks=f.base; block(chunks,11,2,10,stateId(44,0));
    auto slab=f.open(chunks,10.5f,2,10.5f); advance(slab,4,90);
    near(slab.playerY(),2.5f);
    chunks=f.base; block(chunks,11,2,10,stateId(1));
    auto cube=f.open(chunks,10.5f,2,10.5f);
    advance(cube,4,90); near(cube.playerY(),3); // lifted onto the cube top
    advance(cube,16,90); near(cube.playerY(),2); assert(cube.playerX()>13); // walks off beyond
    // Upside-down stairs present a full-height face on their tall side and are
    // only walkable over the low half.
    chunks=f.base; block(chunks,11,2,10,stateId(53,4));
    auto flipped=f.open(chunks,10.5f,2,10.5f); advance(flipped,20,90);
    near(flipped.playerY(),2); assert(flipped.playerX()>13);
    // A low ceiling must veto the step: no lift fits, so the body stays put.
    chunks=f.base; block(chunks,11,2,10,stateId(1)); block(chunks,10,4,10,stateId(1));
    auto ceiling=f.open(chunks,10.5f,2,10.5f); advance(ceiling,20,90);
    near(ceiling.playerY(),2); near(ceiling.playerX(),10.75f);
    std::cout<<"stairs: 10 tile ids x 8 states halves, facings, merge/lock/inner, "
               "4-way step up, slab/cube/flipped/ceiling OK\n";
}
void unknownStreamingWall() {
    Temp root; World fixture; fixture.create(12345,root.path,true,864,false,false);
    const int cx=int(fixture.playerX())/16, cz=int(fixture.playerZ())/16;
    Bytes bytes {'M','C','O','C','H','N','K',0};
    auto append=[&](uint32_t v) { for (int i=0;i<4;++i) bytes.push_back(uint8_t(v>>(i*8))); };
    const int key=cz*(fixture.width()/16)+cx;
    append(2); append(fixture.seed()); append(hash(read(fixture.path()+"/level.json"))); append(uint32_t(key)); append(256);
    for (int z=0;z<16;++z) for (int x=0;x<16;++x) bytes.push_back(uint8_t(fixture.biomeAt(cx*16+x,cz*16+z)));
    for (int z=0;z<16;++z) for (int x=0;x<16;++x) { append(uint32_t((179*16+z)*16+x)); append(stateId(1)); }
    append(hash(bytes)); write(fixture.path()+"/chunk-"+std::to_string(key)+".bin",bytes);
    const float edge=float((cx+1)*16);
    player(fixture.path(),edge-1,180,float(cz*16)+8.5f);
    World world; world.beginStreaming(1); world.open(fixture.path(),root.path);
    const auto count=world.streamingInfo().totalGenerated;
    assert(!world.chunkLoaded(int(edge),int(world.playerZ())));
    advance(world,20); near(world.playerX(),edge-.25f); near(world.playerY(),180);
    assert(world.streamingInfo().totalGenerated==count); // collision must not generate unknown chunks
    std::cout<<"block collision: unloaded streaming chunks remain walls without generation OK\n";
}
int main() { sourceBoxes(); staticBlockBoxes(); worldCollision(); climbing(); stairStepUp(); unknownStreamingWall(); }
