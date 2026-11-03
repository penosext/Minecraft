#pragma once

#include "World.hpp"
#include <functional>
#include <vector>

namespace sourceitems { struct ItemSpec; }
namespace mcworld {
// Placement is a source-item -> world-state operation. These are render indices,
// never item IDs, inventory entries, or a second item registry.
using PlacementSpec = sourceitems::ItemSpec;
struct PlacementChange { int x, y, z, renderIndex; };
struct PlacementContext {
    BlockHit hit;
    float yaw = 0, clickHeight = 0;
    float playerX = 0, playerY = 0, playerZ = 0; // feet, World yaw convention
    std::function<uint16_t(int, int, int)> blockAt;
    std::function<bool(int, int, int)> available;
};
const PlacementSpec* placementItem(int sourceItemId, int aux);
// Exact catalog metadata only; no representative-orientation fallback.
int placementState(int tileId, int data);
// Audited source subset, NOT a Tile engine: no ticks, redstone, growth, neighbor
// physics, double chests, or unsupported catalog states. Failure clears output.
// World validates player AABB/saved palette and atomically publishes the plan.
bool planItemPlacement(const PlacementSpec& item, const PlacementContext& context,
                       std::vector<PlacementChange>& changes);
} // namespace mcworld
