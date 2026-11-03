#pragma once

#include <array>
#include <cstdint>
#include <functional>

namespace mcworld {
// Source-audited Tile::getAABB/addAABBs subset, NOT the full Tile/Entity engine.
// Never inferred from render mesh/shape names. Catalog IDs are index + 1.
struct CollisionState { int tileId = 0, data = 0; };
struct CollisionBox { float x0, y0, z0, x1, y1, z1; };
struct CollisionBoxes {
    std::array<CollisionBox, 5> boxes {};
    int count = 0;
    // Unknown IDs/unsupported metadata are conservative full cells, not air.
    bool conservative = false;
};
struct CollisionContext {
    std::function<uint16_t(int, int, int)> blockAt;
    // Missing/out-of-world neighbors never cause generation. World separately
    // makes unavailable cells barriers, including unloaded streaming chunks.
    std::function<bool(int, int, int)> available;
    // Optional: resolve a neighbor straight to its tile/data, skipping the
    // render-id lookup. Used for tiles that have no catalog render state yet;
    // when unset, neighbors resolve through blockAt + collisionState as before.
    // Defaulted so existing brace-initialized call sites stay warning-free.
    std::function<CollisionState(int, int, int)> stateAt = nullptr;
};
CollisionState collisionState(uint16_t catalogBlock);
bool collisionLadder(CollisionState state); // ladder or vine; feet-cell test
// Climb test for the feet cell. Ladders carry their supporting face in data and
// are culled by the source when it goes away, so they climb on tile id alone.
// Vines additionally need a real attachment: the source only keeps a vine alive
// while a cube-shaped neighbour or a roof holds it, and this port runs no block
// ticks, so an unsupported vine left in saved data must not be climbable.
// Independent implementation, behaviour verified against observed game rules.
bool collisionClimbable(CollisionState state, int x, int y, int z,
                        const CollisionContext& context = {});
bool collisionIntersects(const CollisionBox& a, const CollisionBox& b);
// Absolute world boxes; fence and cobblestone wall extend to y+1.5, so
// candidate queries must also inspect the cell below the entity.
// No fluid forces, damage or block updates.
// Static-block shapes added in this pass (carpet 1/16 sheet, wall 1.5 post,
// enchantment table 3/4 height, anvil narrowed on its facing axis, brick/birch/
// jungle/quartz stairs, stained glass pane):
// independent implementation, behaviour verified against observed game rules.
CollisionBoxes blockCollisionBoxes(CollisionState state, int x, int y, int z,
                                   const CollisionContext& context = {});
} // namespace mcworld
