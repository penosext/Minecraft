#include "BlockInteraction.hpp"
#include "BlockCollision.hpp"
#include "ItemPlacement.hpp"
#include "ItemCatalog.hpp"
#include "LegacyCatalog.hpp"

namespace mcworld {
int interactionState(int tileId, int data) {
    // Reuse placement's exact-key lookup so the two paths can never disagree
    // about which (tileId, data) pairs the embedded catalog can represent.
    return placementState(tileId, data);
}

namespace {
using State = CollisionState;
// Reuse the existing render-index -> source (tileId, data) reverse map rather
// than adding a second one. Unknown indices come back as {-1,-1}.
State stateOf(uint16_t block) { return collisionState(block); }

// DoorTile.h:19-24 - the exact source mask names and values.
//   UPPER_BIT = 8, C_DIR_MASK = 3, C_OPEN_MASK = 4,
//   C_LOWER_DATA_MASK = 7, C_IS_UPPER_MASK = 8, C_RIGHT_HINGE_MASK = 16
constexpr int UpperBit = 8, OpenMask = 4, LowerDataMask = 7, IsUpperMask = 8;

// DoorTile.cpp:318-338 DoorTile::getCompositeData. The lower half owns
// direction and the open bit; the hinge side is bit0 of the UPPER half's data.
// A missing neighbour reads as data 0, matching Level::getData on air.
int compositeData(const State& self, const State& neighbour) {
    const bool isUpper = (self.data & UpperBit) != 0;
    const int lowerData = isUpper ? neighbour.data : self.data;
    const int upperData = isUpper ? self.data : neighbour.data;
    const bool isRightHinge = (upperData & 1) != 0;
    return (lowerData & LowerDataMask) | (isUpper ? 8 : 0) | (isRightHinge ? 16 : 0);
}
} // namespace

bool interactionSupported(int tileId) {
    switch (tileId) {
    // DoorTile, registered twice: Tile.cpp:331 door_wood (Material::wood) and
    // Tile.cpp:339 door_iron (Material::metal). Both are "handled" by source
    // use(); only the wooden one mutates. Catalog ships data 0-11 for each.
    case 64: case 71: return true;
    // Everything else is refused. Grouped by the reason, which is NOT
    // interchangeable - see tools/extract-interaction.py for the per-tile audit:
    //  * lever 69 / button 77 / repeater 93: source use() only flips the
    //    powered bit (LeverTile.cpp:240, ButtonTile.cpp:205), but the embedded
    //    catalog has a single data=0 render state for each, so the toggled
    //    state has no render index at all. Writing it is impossible, not merely
    //    cosmetic, and inventing/aliasing a state is exactly the kind of fake
    //    implementation this port refuses. Needs catalog state generation first.
    //  * button 77 additionally depends on addToTickNextTick
    //    (ButtonTile.cpp:212) to pop back out; with no tick scheduler it could
    //    only ever stay pressed.
    //  * chest 54, furnace 61, dispenser 23, workbench 58, brewing stand,
    //    beacon, etc: source use() opens a container/GUI via a tile entity
    //    (ChestTile.cpp:265, FurnaceTile.cpp:111, WorkbenchTile.cpp:35). No
    //    container model exists here and a dummy screen would be a fake UI.
    //  * trapdoor 96, fence gate 107, bed 26, cake 92, jukebox 84, note block
    //    84, anvil 145, comparator 149/150, hopper 154, enchanting table 116,
    //    brewing stand 117: absent from the catalog entirely.
    //  * crops 59/141/142, farmland 60, cocoa, nether wart: no use() override
    //    exists upstream at all; right-click growth is DyePowderItem::useOn,
    //    an item-side behaviour outside this block-interaction scope.
    default: return false;
    }
}

InteractionResult planBlockInteraction(const InteractionContext& c,
                                       std::vector<InteractionChange>& changes) {
    changes.clear();
    const auto& hit = c.hit;
    if (!hit.hit || !c.blockAt || !c.available) return InteractionResult::Unhandled;
    if (!hit.block || hit.block > legacy::kBlockCount) return InteractionResult::Unhandled;
    // MultiPlayerGameMode.cpp:297: a sneaking player holding an item skips
    // use() entirely so the item can be placed against the block.
    if (c.sneaking && c.holdingItem) return InteractionResult::Unhandled;
    if (!c.available(hit.x, hit.y, hit.z)) return InteractionResult::Unhandled;

    // hit.block is a catalog index + 1, NOT a source TileID; resolve the real
    // source (tileId, data) before applying any source rule.
    const State self = stateOf(hit.block);
    if (!interactionSupported(self.tileId) || self.data < 0) return InteractionResult::Unhandled;

    if (self.tileId == 64 || self.tileId == 71) {
        // DoorTile::use, DoorTile.cpp:185-215.
        // :197 `if (material == Material::metal) return true;` - the iron door
        // consumes the click and changes nothing. Source-faithful side effect:
        // you also cannot place a block against an iron door face. Reported as
        // Consumed, i.e. handled, so the caller suppresses placement like :330.
        if (self.tileId == 71) return InteractionResult::Consumed;

        const bool isUpper = (self.data & IsUpperMask) != 0;
        // The other half of this door. Upper halves pair downward.
        const int otherY = isUpper ? hit.y - 1 : hit.y + 1;
        if (!c.available(hit.x, otherY, hit.z)) return InteractionResult::Unhandled;
        const State other = stateOf(c.blockAt(hit.x, otherY, hit.z));
        // Source neighborChanged removes an unpaired half; this subset has no
        // block updates, so refuse rather than toggle a half-formed door.
        if (other.tileId != self.tileId || ((other.data & UpperBit) != 0) == isUpper)
            return InteractionResult::Unhandled;

        const int composite = compositeData(self, other);
        // :200-201 lowerData = composite & C_LOWER_DATA_MASK; lowerData ^= 4.
        const int lowerData = (composite & LowerDataMask) ^ OpenMask;
        // :202-211 only the LOWER half's data is ever written; the upper half
        // (which carries the hinge bit) is left untouched. Clicking the upper
        // half writes to y-1. Both cells are still marked dirty, which here is
        // World's revision bump rather than setTilesDirty.
        const int lowerY = isUpper ? hit.y - 1 : hit.y;
        const int render = interactionState(self.tileId, lowerData);
        if (render < 0) return InteractionResult::Unhandled;
        changes.push_back({hit.x, lowerY, hit.z, render});
        return InteractionResult::Changed;
    }
    return InteractionResult::Unhandled;
}
} // namespace mcworld
