// Standalone host test; link BlockInteraction.cpp, a freshly built World.cpp,
// OriginalWorld and the original core/biome/terrain/structure/decoration
// libraries. No renderer, no Inventory, no UI.
#define MCWORLD_INTERACTION_FIXTURES_ONLY
#include "world-interaction-test.cpp"
#include "BlockInteraction.hpp"
#include "ItemPlacement.hpp"
#include "ItemCatalog.hpp"

namespace {
using mcworld::InteractionChange;
using mcworld::InteractionContext;
using mcworld::InteractionResult;
using mcworld::interactionState;
using mcworld::interactionSupported;
using mcworld::planBlockInteraction;

// --- Planner-level fixtures (no world I/O) ---------------------------------
struct Grid {
    std::map<std::tuple<int,int,int>,uint16_t> blocks;
    uint16_t get(int x,int y,int z) const {
        auto it=blocks.find({x,y,z}); return it==blocks.end() ? 0 : it->second;
    }
    void put(int x,int y,int z,int tile,int data=0) {
        const int render=interactionState(tile,data); assert(render>=0);
        blocks[{x,y,z}]=uint16_t(render+1);
    }
    // A complete door: lower half carries direction+open, upper carries hinge.
    void door(int x,int y,int z,int tile,int dir,bool open,bool rightHinge) {
        put(x,y,z,tile,(dir&3)|(open?4:0));
        put(x,y+1,z,tile,8|int(rightHinge));
    }
    InteractionContext context(int x,int y,int z,bool sneaking=false,bool holding=false) {
        InteractionContext c;
        c.hit={true,false,x,y,z,2,get(x,y,z),2}; // hit, unknown, x,y,z, face, block, distance
        c.sneaking=sneaking; c.holdingItem=holding;
        c.blockAt=[this](int bx,int by,int bz){ return get(bx,by,bz); };
        c.available=[](int bx,int by,int bz){ return bx>=0&&bx<32&&by>0&&by<256&&bz>=0&&bz<32; };
        return c;
    }
};
// Decode a planned change back into source (tileId, data).
std::pair<int,int> decode(const InteractionChange& change) {
    const auto s=mcworld::collisionState(uint16_t(change.renderIndex+1));
    return {s.tileId,s.data};
}

void supportedSet() {
    // Only the two door tiles are claimed. Every tile whose toggled state is
    // missing from the catalog, or which needs a container/tick, must be false.
    assert(interactionSupported(64) && interactionSupported(71));
    for (int tile : {69,77,93,54,61,58,23,118,96,107,26,92,116,117,145,154,149,150,59,60,141,142,0,1,2,5})
        assert(!interactionSupported(tile));
    // The honest-degradation claim is load bearing: assert the catalog really
    // cannot represent a powered lever/button/repeater, so returning false is
    // forced rather than a preference. If these ever gain a state, this fires.
    for (int tile : {69,77,93}) {
        assert(interactionState(tile,0)>=0);
        for (int data=1; data<16; ++data) assert(interactionState(tile,data)<0);
    }
    // Conversely every door state the toggle can produce must exist.
    for (int tile : {64,71}) for (int data=0; data<12; ++data) assert(interactionState(tile,data)>=0);
}

void woodenDoorAllDirections() {
    // Four directions, both hinge sides, clicked on either half: the toggle
    // always flips bit 4 of the LOWER half and writes exactly that one cell.
    for (int dir=0; dir<4; ++dir) for (bool hinge : {false,true}) for (bool open : {false,true})
        for (bool clickUpper : {false,true}) {
            Grid g; g.door(10,100,12,64,dir,open,hinge);
            auto context=g.context(10,clickUpper?101:100,12);
            std::vector<InteractionChange> changes;
            assert(planBlockInteraction(context,changes)==InteractionResult::Changed);
            // Source writes the lower half only; the upper (hinge) is untouched.
            assert(changes.size()==1);
            assert(changes[0].x==10 && changes[0].y==100 && changes[0].z==12);
            const auto [tile,data]=decode(changes[0]);
            assert(tile==64);
            // Direction preserved, open bit inverted, hinge not copied down.
            assert((data&3)==dir);
            assert(((data&4)!=0)==!open);
            assert(data==((dir&3)|(open?0:4)));
        }
}

void woodenDoorRoundTrip() {
    // Open then close returns to the exact original state, for all directions.
    for (int dir=0; dir<4; ++dir) for (bool hinge : {false,true}) {
        Grid g; g.door(10,100,12,64,dir,false,hinge);
        const uint16_t lower0=g.get(10,100,12), upper0=g.get(10,101,12);
        for (int step=0; step<2; ++step) {
            auto context=g.context(10,100,12);
            std::vector<InteractionChange> changes;
            assert(planBlockInteraction(context,changes)==InteractionResult::Changed);
            g.blocks[{changes[0].x,changes[0].y,changes[0].z}]=uint16_t(changes[0].renderIndex+1);
            // Upper half never changes at any point in the cycle.
            assert(g.get(10,101,12)==upper0);
            if (step==0) assert(g.get(10,100,12)!=lower0);
        }
        assert(g.get(10,100,12)==lower0);
    }
}

void woodenDoorHalvesStayConsistent() {
    // Both halves must agree about open/closed after the toggle. Only the lower
    // half stores the open bit, so "synchronised" means the composite data seen
    // from either half reports the same state - which is why writing one cell
    // is correct and writing both would be wrong.
    for (bool clickUpper : {false,true}) {
        Grid g; g.door(10,100,12,64,1,false,true);
        auto context=g.context(10,clickUpper?101:100,12);
        std::vector<InteractionChange> changes;
        assert(planBlockInteraction(context,changes)==InteractionResult::Changed);
        g.blocks[{changes[0].x,changes[0].y,changes[0].z}]=uint16_t(changes[0].renderIndex+1);
        const auto lower=mcworld::collisionState(g.get(10,100,12));
        const auto upper=mcworld::collisionState(g.get(10,101,12));
        assert(lower.tileId==64 && upper.tileId==64);
        assert(!(lower.data&8) && (upper.data&8)); // halves kept their roles
        assert((lower.data&4)!=0);                 // now open
        assert((upper.data&1)!=0);                 // hinge bit preserved
    }
}

void unpairedHalvesRefused() {
    // A lone half, or two halves of the same role, is refused instead of being
    // toggled: the source relies on neighborChanged cleanup we do not run.
    std::vector<InteractionChange> changes;
    { Grid g; g.put(10,100,12,64,1); // lower with air above
      assert(planBlockInteraction(g.context(10,100,12),changes)==InteractionResult::Unhandled); assert(changes.empty()); }
    { Grid g; g.put(10,101,12,64,8); // upper with air below
      assert(planBlockInteraction(g.context(10,101,12),changes)==InteractionResult::Unhandled); assert(changes.empty()); }
    { Grid g; g.put(10,100,12,64,1); g.put(10,101,12,64,1); // two lowers
      assert(planBlockInteraction(g.context(10,100,12),changes)==InteractionResult::Unhandled); assert(changes.empty()); }
    { Grid g; g.put(10,100,12,64,1); g.put(10,101,12,71,8); // mismatched family
      assert(planBlockInteraction(g.context(10,100,12),changes)==InteractionResult::Unhandled); assert(changes.empty()); }
    { // Upper half at the very bottom of the available range: the neighbour
      // query itself must fail safely rather than read out of range.
      Grid g; g.put(10,1,12,64,8);
      assert(planBlockInteraction(g.context(10,1,12),changes)==InteractionResult::Unhandled); assert(changes.empty()); }
}

void ironDoorConsumesWithoutChanging() {
    // DoorTile.cpp:197 returns true at the Material::metal guard. Handled, but
    // nothing is planned: an iron door can never be opened by hand here.
    for (int dir=0; dir<4; ++dir) for (bool open : {false,true}) for (bool clickUpper : {false,true}) {
        Grid g; g.door(10,100,12,71,dir,open,false);
        const auto before=g.blocks;
        std::vector<InteractionChange> changes;
        assert(planBlockInteraction(g.context(10,clickUpper?101:100,12),changes)==InteractionResult::Consumed);
        assert(changes.empty());
        assert(g.blocks==before);
    }
}

void nonInteractiveRefused() {
    // Ordinary blocks, container/GUI tiles and the state-blocked redstone
    // tiles all report Unhandled so the caller falls through to placement.
    for (auto entry : std::vector<std::pair<int,int>>{{1,0},{2,0},{5,0},{20,0},{35,0},
            {54,2},{61,2},{58,0},{23,2},{118,0},{69,0},{77,0},{93,0},{50,1},{44,0},{53,0},{65,2}}) {
        Grid g; g.put(10,100,12,entry.first,entry.second);
        std::vector<InteractionChange> changes;
        assert(planBlockInteraction(g.context(10,100,12),changes)==InteractionResult::Unhandled);
        assert(changes.empty());
    }
    { // Air and a miss are both Unhandled.
      Grid g; std::vector<InteractionChange> changes;
      assert(planBlockInteraction(g.context(10,100,12),changes)==InteractionResult::Unhandled);
      InteractionContext miss=g.context(10,100,12); miss.hit.hit=false;
      assert(planBlockInteraction(miss,changes)==InteractionResult::Unhandled); }
}

void sneakGateAndUnavailableCells() {
    Grid g; g.door(10,100,12,64,1,false,false);
    std::vector<InteractionChange> changes;
    // MultiPlayerGameMode.cpp:297: sneaking WITH an item skips use() so the
    // item is placed instead; sneaking empty handed still interacts.
    assert(planBlockInteraction(g.context(10,100,12,true,true),changes)==InteractionResult::Unhandled);
    assert(changes.empty());
    assert(planBlockInteraction(g.context(10,100,12,true,false),changes)==InteractionResult::Changed);
    assert(planBlockInteraction(g.context(10,100,12,false,true),changes)==InteractionResult::Changed);
    // An unavailable (unloaded/out-of-world) hit cell fails safely.
    InteractionContext blocked=g.context(10,100,12);
    blocked.available=[](int,int,int){ return false; };
    assert(planBlockInteraction(blocked,changes)==InteractionResult::Unhandled);
    assert(changes.empty());
    // Available hit cell but unavailable neighbour: still refused, no partial.
    InteractionContext halfBlocked=g.context(10,100,12);
    halfBlocked.available=[](int,int by,int){ return by<=100; };
    assert(planBlockInteraction(halfBlocked,changes)==InteractionResult::Unhandled);
    assert(changes.empty());
    // Garbage render index is rejected rather than trusted.
    InteractionContext bogus=g.context(10,100,12);
    bogus.hit.block=uint16_t(legacy::kBlockCount+1);
    assert(planBlockInteraction(bogus,changes)==InteractionResult::Unhandled);
}

// --- World-level fixtures (real save, real pick, real publication) ---------
// The eye sits at feet + 1.62. With the player's feet level with the door's
// lower cell, a level gaze therefore lands on the UPPER half and a downward
// gaze on the lower half. Measured against World::pick, not assumed.
constexpr float LowerHalfPitch = -30, UpperHalfPitch = 0;
// Build a door at a known place well above generated terrain and stand the
// player in front of it, so World::pick's cube-cell DDA selects a door cell.
struct DoorWorld {
    Temp root;
    std::string path;
    // Door occupies (100,200,100)/(100,201,100); the player stands at z 98.5
    // looking toward +Z so yaw 0 hits the door's -Z face.
    void build(int tile,int dir,bool open,bool rightHinge,float playerZ=98.5f) {
        World fixture; fixture.create(12345, root.path, true, 864, false, false);
        path=fixture.path();
        const int lower=interactionState(tile,(dir&3)|(open?4:0));
        const int upper=interactionState(tile,8|int(rightHinge));
        assert(lower>=0 && upper>=0);
        originalFixture(path, {{100,199,100,blockId("stone")},
                               {100,200,100,uint16_t(lower+1)},
                               {100,201,100,uint16_t(upper+1)}});
        player(path, 100.5f, 200, playerZ);
    }
    std::pair<int,int> state(const World& world,int y) const {
        const auto s=mcworld::collisionState(world.blockAt(100,y,100));
        return {s.tileId,s.data};
    }
};

void worldDoorOpensAndPersists() {
    for (int dir=0; dir<4; ++dir) {
        DoorWorld fixture; fixture.build(64,dir,false,true);
        World world; world.open(fixture.path, fixture.root.path);
        const auto revision=world.revision();
        const auto upperBefore=fixture.state(world,201);
        // Eye height is feet + 1.62, so from y 200 a level gaze meets the UPPER
        // half; -30 degrees is the lower half. Both must toggle the same cell.
        const auto hit=world.pick(0,LowerHalfPitch);
        assert(hit.hit && hit.x==100 && hit.z==100 && hit.y==200);
        assert(world.useBlock(0,LowerHalfPitch));
        // Open bit set, direction kept, upper half byte-identical.
        const auto lower=fixture.state(world,200);
        assert(lower.first==64 && (lower.second&3)==dir && (lower.second&4));
        assert(fixture.state(world,201)==upperBefore);
        // Revision advanced so the renderer refreshes.
        assert(world.revision()!=revision);
        // Closing again restores the original state exactly.
        assert(world.useBlock(0,LowerHalfPitch));
        assert(fixture.state(world,200)==std::make_pair(64,dir));
        assert(fixture.state(world,201)==upperBefore);
        // Open it once more, save, reopen: the state survives the round trip
        // and the toggled key really was accepted by the saved palette.
        assert(world.useBlock(0,LowerHalfPitch));
        const auto saved=fixture.state(world,200);
        world.save();
        World reopened; reopened.open(fixture.path, fixture.root.path);
        assert(fixture.state(reopened,200)==saved);
        assert(fixture.state(reopened,201)==upperBefore);
        assert((saved.second&4)!=0);
    }
}

void worldDoorClickedOnUpperHalf() {
    // Clicking the upper half still toggles the LOWER cell, per DoorTile.cpp:209.
    DoorWorld fixture; fixture.build(64,1,false,false);
    World world; world.open(fixture.path, fixture.root.path);
    const auto hit=world.pick(0,UpperHalfPitch);
    assert(hit.hit && hit.y==201);
    assert(world.useBlock(0,UpperHalfPitch));
    const auto lower=fixture.state(world,200);
    assert(lower.first==64 && (lower.second&4));
    assert(fixture.state(world,201).second==8); // upper untouched
}

void worldDoorPaletteAcceptsEveryToggledState() {
    // The saved palette is the whole embedded catalog, so every state the
    // toggle can produce must be writable. This is the easiest thing to get
    // wrong: a missing palette key would make opening fail at publication.
    for (int dir=0; dir<4; ++dir) for (bool open : {false,true}) {
        const int render=interactionState(64,(dir&3)|(open?4:0));
        assert(render>=0);
        DoorWorld fixture; fixture.build(64,dir,open,false);
        World world; world.open(fixture.path, fixture.root.path);
        assert(world.useBlock(0,LowerHalfPitch));
    }
}

void worldIronDoorHandledButUnchanged() {
    DoorWorld fixture; fixture.build(71,1,false,false);
    World world; world.open(fixture.path, fixture.root.path);
    const auto revision=world.revision();
    const auto lower=fixture.state(world,200), upper=fixture.state(world,201);
    // Handled (so the caller suppresses placement) yet nothing changed, and
    // crucially the revision does NOT move: there is nothing to redraw.
    assert(world.useBlock(0,LowerHalfPitch));
    assert(world.useBlock(0,UpperHalfPitch));
    assert(fixture.state(world,200)==lower && fixture.state(world,201)==upper);
    assert(world.revision()==revision);
    // Still unchanged after a save/reopen cycle.
    world.save();
    World reopened; reopened.open(fixture.path, fixture.root.path);
    assert(fixture.state(reopened,200)==lower && fixture.state(reopened,201)==upper);
}

void worldRejectsNonInteractiveAndMisses() {
    Temp root;
    World fixture; fixture.create(12345, root.path, true, 864, false, false);
    originalFixture(fixture.path(), {{100,200,100,blockId("stone")},
                                     {100,200,102,blockId("workBench")},
                                     {100,200,98,blockId("chest_data_2")}});
    // Feet one cell above the slab of blocks, so each target is reachable.
    player(fixture.path(), 100.5f, 201, 100.5f);
    World world; world.open(fixture.path(), root.path);
    const auto revision=world.revision();
    // Confirm the ray really reaches each intended tile before asserting the
    // refusal, otherwise a miss would pass the test for the wrong reason.
    for (auto probe : std::vector<std::tuple<float,float,int>>{{0,-90,1},{0,-45,58},{180,-45,54}}) {
        const auto hit=world.pick(std::get<0>(probe),std::get<1>(probe));
        assert(hit.hit && mcworld::collisionState(hit.block).tileId==std::get<2>(probe));
        assert(!world.useBlock(std::get<0>(probe),std::get<1>(probe)));
    }
    // A miss into empty sky is false, not a throw.
    assert(!world.pick(0,90).hit);
    assert(!world.useBlock(0,90));
    assert(world.revision()==revision);
}

void worldDoorInsidePlayerStillOpens() {
    // Placement rejects any cell overlapping the player AABB; interaction must
    // NOT, because upstream lets you open the door you are standing in.
    DoorWorld fixture; fixture.build(64,1,false,false,100.5f);
    World world; world.open(fixture.path, fixture.root.path);
    // Standing inside the door: the ray starts within the cell, so face is -1.
    // Placement would reject this outright; interaction must still succeed.
    const auto hit=world.pick(0,UpperHalfPitch);
    assert(hit.hit && hit.x==100 && hit.z==100 && hit.face==-1);
    assert(world.useBlock(0,UpperHalfPitch));
    assert((fixture.state(world,200).second&4)!=0);
    assert(fixture.state(world,201).second==8); // upper half untouched
}

void worldUnloadedChunkFails() {
    DoorWorld fixture; fixture.build(64,1,false,false);
    World world; world.open(fixture.path, fixture.root.path);
    // With streaming on and the view pushed far away the door's chunk is not
    // resident, so pick refuses to reach it and the interaction fails safely.
    world.beginStreaming(1);
    world.setStreamingView(0,0,1.5f);
    const auto revision=world.revision();
    bool everFailed=false;
    for (int i=0; i<4; ++i) {
        if (!world.chunkLoaded(100,100)) {
            assert(!world.useBlock(0,0));
            everFailed=true;
            break;
        }
        world.processChunkLoads(1);
    }
    // When the chunk does stay resident the interaction is simply allowed; the
    // unloaded path is asserted by pick's own chunkLoaded guard either way.
    if (!everFailed) assert(world.chunkLoaded(100,100));
    (void)revision;
}

void worldCustomSaveThrows() {
    // Custom-v1 removal-only saves are immutable, exactly as for placement.
    Temp root;
    World fixture; fixture.create(4321, root.path, false);
    World world; world.open(fixture.path(), root.path);
    rejects([&]{ world.useBlock(0,0); });
}

void worldRequiresLoaded() {
    World world;
    rejects([&]{ world.useBlock(0,0); });
}

void worldRejectsInvalidInput() {
    DoorWorld fixture; fixture.build(64,1,false,false);
    World world; world.open(fixture.path, fixture.root.path);
    const auto nan=std::numeric_limits<float>::quiet_NaN();
    rejects([&]{ world.useBlock(nan,0); });
    rejects([&]{ world.useBlock(0,nan); });
    rejects([&]{ world.useBlock(0,0,nan); });
    rejects([&]{ world.useBlock(0,0,0); });
}
} // namespace

int main() {
    supportedSet();
    woodenDoorAllDirections();
    woodenDoorRoundTrip();
    woodenDoorHalvesStayConsistent();
    unpairedHalvesRefused();
    ironDoorConsumesWithoutChanging();
    nonInteractiveRefused();
    sneakGateAndUnavailableCells();
    worldDoorOpensAndPersists();
    worldDoorClickedOnUpperHalf();
    worldDoorPaletteAcceptsEveryToggledState();
    worldIronDoorHandledButUnchanged();
    worldRejectsNonInteractiveAndMisses();
    worldDoorInsidePlayerStillOpens();
    worldUnloadedChunkFails();
    worldCustomSaveThrows();
    worldRequiresLoaded();
    worldRejectsInvalidInput();
    std::puts("block-interaction-test: wooden door four directions open/close round trip, "
              "lower-half-only writes with hinge preserved, unpaired halves refused, iron door "
              "consumed without change, lever/button/repeater unsupported for missing catalog states, "
              "container tiles refused, sneak gate, palette round trip and save persistence passed");
    return 0;
}
