#pragma once
//
// Pure furnace tables ported from the pinned upstream sources. No World,
// Inventory or file I/O; BlockEntities owns the furnace state machine.
//
//   * smeltResult  = FurnaceRecipes::FurnaceRecipes (FurnaceRecipes.cpp:18-44).
//     Recipes key on the INPUT ITEM ID ONLY (recipies[itemId]); input aux is
//     ignored, which is why every log variant smelts to charcoal (:32).
//   * burnDuration = FurnaceTileEntity::getBurnDuration
//     (FurnaceTileEntity.cpp:276-330), BURN_INTERVAL = 10*20 (:20).
//
// Recipes/fuels whose item is not in this port's ItemCatalog simply never
// match at runtime (the catalog has no Item for them), but the table keeps
// the full source list so a later catalog extension activates them unchanged.
namespace smelting {

constexpr int kBurnInterval = 200; // FurnaceTileEntity.cpp:20, 10 * 20 ticks.

struct Result {
    bool ok = false;
    int itemId = -1;
    int aux = 0;
    int count = 0;
};

// FurnaceRecipes lookup by source input item id. Unknown ids return ok=false.
Result smeltResult(int inputItemId);

// FurnaceTileEntity::getBurnDuration by source item id + aux. 0 = not fuel.
int burnDuration(int itemId, int aux);

// Every source furnace input id, in FurnaceRecipes.cpp order (for tests/UI).
int recipeInputCount();
int recipeInput(int index);

} // namespace smelting
