#pragma once

#include <vector>

namespace mcgame {

enum class ToolKind { None, Sword, Shovel, Pickaxe, Axe, Hoe, Armor, Shears };

struct ToolProperties {
    int itemId = 0, maxDamage = 0, armorSlot = -1, defense = 0, tierLevel = 0;
    ToolKind kind = ToolKind::None;
    float speed = 1, attackDamage = 1;
};

// Immutable catalog properties; nullptr for items without tool/armor properties.
const ToolProperties* toolProperties(int itemId);
// Total player melee damage (base player damage plus the held item's attribute).
float attackDamage(int itemId);
// Unknown blocks return negative hardness and cannot be broken by these rules.
float blockHardness(int tileId);
bool canHarvest(int itemId, int tileId, int data = 0);
// Per-tick survival progress: no haste, enchantments, or random drop simulation.
// Water /5 is upstream; grounded is retained for the API but has no effect:
// the pinned console source comments out airborne /5. No effects or water-worker.
float destroyProgress(int itemId, int tileId, int data, bool grounded, bool inWater);
// Durability costs only; stack mutation and the break comparison belong to Inventory.
int miningWear(int itemId, int tileId);
int attackWear(int itemId);

struct BlockDrop { int itemId, aux, count; };
// Deterministic, catalog-representable subset only; no random loot or enchants.
// False distinguishes unaudited/unsupported rules from known empty drops.
bool knownDropRule(int tileId);
std::vector<BlockDrop> drops(int heldItemId, int tileId, int data);

} // namespace mcgame
