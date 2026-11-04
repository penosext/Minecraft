#include "Smelting.hpp"

namespace smelting {
namespace {
struct Recipe { int input, output, aux; };
// FurnaceRecipes.cpp:20-41, same order. Ids from Tile.h / Item.h (_Id constants);
// every product is new ItemInstance(x) => count 1. CoalItem::CHAR_COAL = 1
// (CoalItem.h:15), DyePowderItem::GREEN = 2 / BLUE = 4 (DyePowderItem.cpp:90,92).
constexpr Recipe kRecipes[] = {
    {15, 265, 0},  // ironOre -> ironIngot
    {14, 266, 0},  // goldOre -> goldIngot
    {56, 264, 0},  // diamondOre -> diamond
    {12, 20, 0},   // sand -> glass
    {319, 320, 0}, // porkChop_raw -> porkChop_cooked
    {363, 364, 0}, // beef_raw -> beef_cooked
    {365, 366, 0}, // chicken_raw -> chicken_cooked
    {349, 350, 0}, // fish_raw -> fish_cooked
    {4, 1, 0},     // cobblestone -> stone
    {337, 336, 0}, // Item::clay -> brick
    {82, 172, 0},  // Tile::clay -> clayHardened
    {81, 351, 2},  // cactus -> green dye
    {17, 263, 1},  // treeTrunk (any aux) -> charcoal
    {129, 388, 0}, // emeraldOre -> emerald
    {392, 393, 0}, // potato -> potatoBaked
    {87, 405, 0},  // netherRack -> netherbrick
    {16, 263, 0},  // coalOre -> coal (silk-touch recipe, :38)
    {73, 331, 0},  // redStoneOre -> redStone
    {21, 351, 4},  // lapisOre -> blue dye
    {153, 406, 0}, // netherQuartz ore -> netherQuartz
};

// Tiles constructed with Material::wood (Tile.cpp ctor lines and the *Tile.cpp
// constructors listed): planks 5, treeTrunk 17, noteblock 25, bookshelf 47,
// wood stairs 53/134/135/136 (StairTile inherits base->material, StairTile.cpp:13),
// chest 54, workBench 58, signs 63/68, door_wood 64, pressurePlate_wood 72, jukebox 84, fence 85, trapdoor 96, huge mushrooms
// 99/100, fenceGate 107, wood slabs 125/126 (126 handled separately), trapped
// chest 146, daylightDetector 151. Ladder (LadderTile.cpp:6) and wood button
// (decoration) are NOT wood material, so they are not fuel upstream either.
bool woodMaterialTile(int id) {
    switch (id) {
    case 5: case 17: case 25: case 47: case 53: case 54: case 58: case 63: case 64:
    case 68: case 72: case 84: case 85: case 96: case 99: case 100: case 107: case 125:
    case 134: case 135: case 136: case 146: case 151:
        return true;
    default:
        return false;
    }
}
} // namespace

Result smeltResult(int inputItemId) {
    for (const auto& recipe : kRecipes)
        if (recipe.input == inputItemId) return {true, recipe.output, recipe.aux, 1};
    return {};
}

int burnDuration(int id, int aux) {
    (void)aux; // Source getBurnDuration never reads aux; coal and charcoal are both 1600.
    if (id < 0) return 0;
    if (id < 256) {
        if (id == 126) return kBurnInterval * 3 / 4;   // woodSlabHalf (FurnaceTileEntity.cpp:287)
        if (woodMaterialTile(id)) return kBurnInterval * 3 / 2; // Material::wood (:292)
        if (id == 173) return kBurnInterval * 8 * 10;  // coalBlock (:297)
    }
    // Wooden DiggerItem/WeaponItem/HoeItem tiers (:303-314): shovel 269,
    // pickaxe 270, hatchet 271, sword 268, hoe 290.
    if (id == 268 || id == 269 || id == 270 || id == 271 || id == 290) return kBurnInterval;
    if (id == 280) return kBurnInterval / 2;   // stick (:316)
    if (id == 263) return kBurnInterval * 8;   // coal, either aux (:321)
    if (id == 327) return kBurnInterval * 100; // bucket_lava (:323)
    if (id == 6) return kBurnInterval / 2;     // sapling (:325)
    if (id == 369) return kBurnInterval * 12;  // blazeRod (:327)
    return 0;
}

int recipeInputCount() { return int(sizeof(kRecipes) / sizeof(kRecipes[0])); }
int recipeInput(int index) {
    return index >= 0 && index < recipeInputCount() ? kRecipes[index].input : -1;
}

} // namespace smelting
