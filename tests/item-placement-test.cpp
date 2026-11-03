// Standalone host test; link ItemPlacement.cpp, World/OriginalWorld and original
// core/biome/terrain/structure/decoration libraries. No renderer or Inventory.
#define MCWORLD_INTERACTION_FIXTURES_ONLY
#include "world-interaction-test.cpp"
#include "ItemPlacement.hpp"
#include "ItemCatalog.hpp"
#include <cstdlib>
#include <new>

// Inject allocation failures across batch preparation, not just validation.
static long placementAllocationCountdown = -1;
void* operator new(std::size_t size) {
    if (placementAllocationCountdown == 0) throw std::bad_alloc();
    if (placementAllocationCountdown > 0) --placementAllocationCountdown;
    if (void* result=std::malloc(size ? size : 1)) return result;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

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
        const int render=placementState(tile,data); assert(render>=0);
        blocks[{x,y,z}]=uint16_t(render+1);
    }
    PlacementContext context(int face=2,float yaw=0,float click=.25f) {
        PlacementContext c; c.hit={true,10,100,12,face,get(10,100,12),2};
        c.yaw=yaw; c.clickHeight=click; c.playerX=10.5f; c.playerY=99; c.playerZ=8.5f;
        c.blockAt=[this](int x,int y,int z) { return get(x,y,z); };
        c.available=[](int x,int y,int z) { return x>=0&&x<32&&y>0&&y<256&&z>=0&&z<32; };
        return c;
    }
};
std::vector<PlacementChange> plan(Grid& g,int id,int aux,int face=2,float yaw=0,float click=.25f) {
    const auto* item=placementItem(id,aux); assert(item);
    auto context=g.context(face,yaw,click); std::vector<PlacementChange> changes;
    assert(planItemPlacement(*item,context,changes)); return changes;
}
void state(const PlacementChange& change,int tile,int data) { assert(change.renderIndex==placementState(tile,data)); }
void itemMetadataRules() {
    Grid g; g.put(10,100,12,1);
    // Same source stairs item. World +90 faces +X, upstream yRot=-90.
    for (auto test : std::vector<std::pair<float,int>>{{0,2},{90,0},{180,3},{-90,1}}) {
        state(plan(g,53,0,2,test.first,.25f).at(0),53,test.second);
        state(plan(g,53,0,2,test.first,.75f).at(0),53,test.second|4);
        state(plan(g,53,0,0,test.first,.25f).at(0),53,test.second|4);
        state(plan(g,53,0,1,test.first,.75f).at(0),53,test.second);
    }
    state(plan(g,44,2,2,0,.25f).at(0),44,2);
    state(plan(g,44,2,2,0,.75f).at(0),44,10);
    state(plan(g,44,2,0).at(0),44,10);
    state(plan(g,44,2,1).at(0),44,2);
    g.put(10,100,12,44,2); state(plan(g,44,2,1).at(0),43,2);
    g.put(10,100,12,44,10); state(plan(g,44,2,0).at(0),43,2);
    g.put(10,100,12,1); g.put(10,100,11,44,2);
    state(plan(g,44,2).at(0),43,2); // target-cell conversion
    std::vector<PlacementChange> rejected{{0,0,0,0}};
    assert(!planItemPlacement(*placementItem(44,3),g.context(),rejected) && rejected.empty());
    g.blocks.erase({10,100,11});
    for (int face=0;face<6;++face) state(plan(g,17,2,face).at(0),17,2|(face>=4?4:face>=2?8:0));
    for (auto test : std::vector<std::pair<float,int>>{{0,2},{90,4},{180,3},{-90,5}}) {
        state(plan(g,61,0,2,test.first).at(0),61,test.second);
        state(plan(g,54,0,2,test.first).at(0),54,test.second);
    }
    state(plan(g,5,2).at(0),5,2); state(plan(g,35,14).at(0),35,14);
    state(plan(g,18,2).at(0),18,2); // persistent leaf flag is not a render subtype
    std::vector<PlacementChange> unsupported;
    assert(!planItemPlacement(*placementItem(86,0),g.context(),unsupported));
    assert(!planItemPlacement(*placementItem(120,0),g.context(),unsupported));
    assert(!planItemPlacement(*placementItem(356,0),g.context(),unsupported));
    assert(!placementItem(53,4)); // orientation bits are never item aux
    assert(!placementItem(-1,0));
}
void attachmentSeedsAndSnow() {
    Grid g; g.put(10,100,12,1);
    for (int face=2;face<6;++face) {
        state(plan(g,65,0,face).at(0),65,face);
        const int data[]={0,0,4,3,2,1}; state(plan(g,50,0,face).at(0),50,data[face]);
    }
    state(plan(g,50,0,1).at(0),50,5);
    std::vector<PlacementChange> changes;
    assert(!planItemPlacement(*placementItem(65,0),g.context(1),changes));
    assert(!planItemPlacement(*placementItem(50,0),g.context(0),changes));
    g.put(10,100,12,44,0); // slabs are collision cells but NOT side support
    assert(!planItemPlacement(*placementItem(65,0),g.context(),changes));
    assert(!planItemPlacement(*placementItem(50,0),g.context(),changes));
    g.put(10,100,12,60,0); state(plan(g,295,0,1).at(0),59,0);
    assert(!placementItem(296,0) || !placementItem(296,0)->placeable);
    state(plan(g,391,0,1).at(0),141,0); state(plan(g,392,0,1).at(0),142,0);
    assert(!planItemPlacement(*placementItem(295,0),g.context(2),changes));
    g.put(10,100,12,3);
    assert(!planItemPlacement(*placementItem(295,0),g.context(1),changes));
    g.put(10,100,12,9); state(plan(g,111,0,1).at(0),111,0);
    g.put(10,100,12,8); assert(!planItemPlacement(*placementItem(111,0),g.context(1),changes));
    g.put(10,100,12,78,2); state(plan(g,78,0,1).at(0),78,3);
    g.put(10,100,12,1); state(plan(g,78,0,1).at(0),78,0);
}
void doorRules() {
    Grid g; g.put(10,100,12,1);
    for (auto test : std::vector<std::pair<float,int>>{{0,1},{90,0},{180,3},{-90,2}}) {
        auto result=plan(g,324,0,1,test.first); assert(result.size()==2);
        state(result[0],64,test.second); state(result[1],64,8);
        assert(result[0].y==101 && result[1].y==102);
    }
    auto iron=plan(g,330,0,1); state(iron[0],71,1); state(iron[1],71,8);
    // yaw0 dir1 -> xra=-1; solidRight is x-1, not guessed from click X.
    g.put(9,101,12,1); state(plan(g,324,0,1)[1],64,9);
    g.put(9,101,12,87); state(plan(g,324,0,1)[1],64,9); // source stone-material netherrack
    g.put(9,101,12,46); state(plan(g,324,0,1)[1],64,8); // TNT neverBuildable, not solidRight
    g.blocks.erase({9,101,12});
    g.put(11,101,12,64,1); g.put(11,102,12,64,8);
    state(plan(g,324,0,1)[1],64,9); // neighboring same-material doorLeft
    std::vector<PlacementChange> changes{{0,0,0,0}};
    g.put(10,102,12,1); assert(!planItemPlacement(*placementItem(324,0),g.context(1),changes)&&changes.empty());
    g.blocks.erase({10,102,12}); g.put(10,100,12,44,0);
    assert(!planItemPlacement(*placementItem(324,0),g.context(1),changes));
    g.put(10,100,12,44,8); assert(planItemPlacement(*placementItem(324,0),g.context(1),changes));
    for (int nonsupport : {46,20,79,89,18}) {
        g.put(10,100,12,nonsupport); // material neverBuildable, despite visible full cube
        assert(!planItemPlacement(*placementItem(324,0),g.context(1),changes));
    }
    g.put(10,100,12,87); assert(planItemPlacement(*placementItem(324,0),g.context(1),changes));
    assert(!planItemPlacement(*placementItem(324,0),g.context(2),changes));
}
void batchAllocationSafety() {
    mcworld::OriginalWorld w(12345,864,false,false);
    const auto lower=uint16_t(placementState(64,1)+1), upper=uint16_t(placementState(64,9)+1);
    assert(lower && upper); assert(w.blockAt(100,201,100)==0);
    w.renderVolume(100.5f,100.5f);
    const std::vector<mcworld::OriginalWorld::BlockChange> batch{{100,201,100,lower},{100,202,100,upper}};
    bool completed=false; int failures=0;
    for (long allocation=0;allocation<128&&!completed;++allocation) {
        placementAllocationCountdown=allocation;
        try { completed=w.setBlocks(batch); }
        catch (const std::bad_alloc&) { ++failures; }
        placementAllocationCountdown=-1;
        if (!completed) {
            assert(w.blockAt(100,201,100)==0 && w.blockAt(100,202,100)==0);
            assert(w.modifiedChunks().empty());
            const auto& volume=w.renderVolume(100.5f,100.5f);
            assert(volume.data[(size_t(201)*volume.depth+100-volume.originZ)*volume.width+100-volume.originX]==0);
        }
    }
    assert(completed && failures>=4);
    assert(w.blockAt(100,201,100)==lower && w.blockAt(100,202,100)==upper);
    const auto edits=w.modifiedChunks().begin()->second.edits;
    rejects([&] { w.setBlocks({{101,201,100,lower},{101,256,100,upper}}); });
    assert(w.blockAt(101,201,100)==0 && w.modifiedChunks().begin()->second.edits==edits);
    rejects([&] { w.setBlocks({{101,201,100,lower},{101,201,100,upper}}); });
    w.beginStreaming(1); assert(!w.setBlocks({{100,203,100,lower},{800,203,800,upper}}));
    assert(w.blockAt(100,203,100)==0);
}
void worldPlacementAndRoundtrip() {
    Temp root; World w; w.create(12345,root.path,true,864,false,false);
    const auto stone=blockId("stone");
    originalFixture(w.path(),{{100,200,102,stone},{110,199,103,stone},{109,200,103,stone},{109,201,103,stone},
                             {120,199,103,stone},{120,201,103,stone},{130,198,100,stone}});
    player(w.path(),100.5f,199,100.5f); w.open(w.path(),root.path);
    assert(w.placeItem(0,0,53,0)); // hitY=.62, so upper stairs data6
    assert(w.blockAt(100,200,101)==uint16_t(placementState(53,6)+1));
    const auto level=read(w.path()+"/level.json"); w.save();
    player(w.path(),110.5f,199,100.5f); w.open(w.path(),root.path);
    auto hit=w.pick(0,-12); assert(hit.hit&&hit.face==1&&hit.z==103);
    assert(w.placeItem(0,-12,324,0));
    assert(w.blockAt(110,200,103)==uint16_t(placementState(64,1)+1));
    assert(w.blockAt(110,201,103)==uint16_t(placementState(64,9)+1)); w.save();
    assert(read(w.path()+"/level.json")==level);
    for (const auto& file : chunkFiles(w.path())) assert(get32(read(file),8)==2);
    World reopened; reopened.open(w.path(),root.path);
    assert(reopened.blockAt(100,200,101)==uint16_t(placementState(53,6)+1));
    assert(reopened.blockAt(110,201,103)==uint16_t(placementState(64,9)+1));
    remapOriginalPalette(w.path()); reopened.open(w.path(),root.path);
    assert(reopened.blockAt(110,200,103)==uint16_t(placementState(64,1)+1));
    assert(reopened.blockAt(110,201,103)==uint16_t(placementState(64,9)+1));
    player(w.path(),120.5f,199,100.5f); reopened.open(w.path(),root.path);
    const auto revision=reopened.revision();
    assert(!reopened.placeItem(0,-12,324,0)); // upper obstructed; never half a door
    assert(reopened.blockAt(120,200,103)==0 && reopened.revision()==revision);
    reopened.save(); World rejected; rejected.open(w.path(),root.path);
    assert(rejected.blockAt(120,200,103)==0 && rejected.blockAt(120,201,103)==stone);
    player(w.path(),130.5f,199,100.5f); rejected.open(w.path(),root.path);
    // Player collision rejects the entire placement. Aim down into own feet cell.
    assert(!rejected.placeItem(0,-90,324,0));
    assert(!rejected.placeItem(0,0,53,4) && !rejected.placeItem(0,0,-1,0));
    // No generator/schema upgrade despite adding world-state placements.
    assert(read(w.path()+"/level.json")!=level); // only deliberate palette reorder above
    World legacy; legacy.create(7,root.path,false);
    rejects([&] { legacy.placeItem(0,-90,1,0); });
    // Existing compatibility regression suite is intentionally unchanged.
    oldFormatsAndNetEdits(); placementPersistenceAndDictionary();
}
}
int main() {
    itemMetadataRules(); attachmentSeedsAndSnow(); doorRules(); batchAllocationSafety(); worldPlacementAndRoundtrip();
    std::cout << "item-placement-test: source item/aux, yaw/face metadata, slab merges, log axes, attachments, seed soil, lily, snow, door hinge/two-cell allocation safety, palette roundtrip and old formats passed\n";
}
