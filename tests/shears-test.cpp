// Host regression for Item::shears (Item.cpp:399, ShearsItem.cpp, ToolRecipies.cpp:106):
// catalog identity, recipe, inventory/held sprite, destroy speed, harvest and durability.
#include "Crafting.hpp"
#include "Inventory.hpp"
#include "ItemCatalog.hpp"
#include "ItemIcons.hpp"
#include "OriginalFirstPerson.hpp"
#include "ToolRules.hpp"
#include "World.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <unistd.h>

namespace {
int checks = 0;
void check(bool condition, const std::string& what) {
    ++checks;
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    std::abort();
}
bool near(float a, float b) { return std::fabs(a - b) <= 1e-6f * std::fmax(1.0f, std::fabs(b)); }
constexpr int Shears = 359;
constexpr int Leaves = 18, Web = 30, TallGrass = 31, Wool = 35, Flower = 37, RedstoneDust = 55,
              Vine = 106, TripWire = 132, Stone = 1, Dirt = 3;

void catalog() {
    // Appended after every earlier row: existing itemIndex values are unchanged.
    check(sourceitems::kItemCount == 196, "Catalog grows by exactly one row");
    check(std::string(sourceitems::kItems[194].key) == "item:365:0", "Previous last row keeps its index");
    const int index = sourceitems::itemIndexForIdAux(Shears, 0);
    check(index == 195, "Shears are the last catalog row");
    const auto& item = sourceitems::kItems[index];
    check(std::string(item.key) == "item:359:0" && item.maxStack == 1 && item.creative && !item.placeable &&
          item.renderIndex == -1 && item.tileId == -1, "Shears catalog identity (stack 1, creative, flat item)");
    const auto* spec = sourceitems::findToolArmor("item:359:0");
    check(spec && std::string(spec->sourceClass) == "ShearsItem" && std::string(spec->iconName) == "shears" &&
          spec->durability == 238 && spec->maxStack == 1 && spec->kind == 1, "ShearsItem ctor: setMaxDamage(238)");
    const auto* tool = mcgame::toolProperties(Shears);
    check(tool && tool->kind == mcgame::ToolKind::Shears && tool->maxDamage == 238, "Shears tool properties");
    check(near(mcgame::attackDamage(Shears), 1.0f) && mcgame::attackWear(Shears) == 0,
          "Shears have no attack modifier and Item::hurtEnemy costs nothing");
}

void recipe() {
    const auto recipes = crafting::recipesFor("item:359:0");
    check(recipes.size() == 1, "One source shears recipe");
    const auto& r = recipes[0];
    check(r.width == 2 && r.height == 2 && r.count == 1 && r.group == "tool" &&
          r.origin.rfind("ToolRecipies.cpp:", 0) == 0, "Shears recipe shape/provenance");
    check(r.cells[0].empty && r.cells[3].empty && !r.cells[1].empty && !r.cells[2].empty &&
          r.cells[1].itemId == 265 && r.cells[2].itemId == 265, "Iron ingots on the \" #\"/\"# \" diagonal");
    const std::string iron = "item:265:0";
    crafting::Grid grid(crafting::kGridSize);
    grid[1] = iron; grid[3] = iron;
    auto match = crafting::match(grid);
    check(match.matched && match.productKey == "item:359:0" && match.count == 1, "Diagonal irons craft shears");
    crafting::Grid mirrored(crafting::kGridSize);
    mirrored[4] = iron; mirrored[8] = iron;
    match = crafting::match(mirrored);
    check(match.matched && match.productKey == "item:359:0", "Mirrored, offset diagonal also crafts shears");
    crafting::Grid row(crafting::kGridSize);
    row[0] = iron; row[1] = iron;
    check(!crafting::match(row).matched || crafting::match(row).productKey != "item:359:0",
          "Two irons side by side are not shears");
}

void sprites() {
    const auto* icon = itemicons::findIcon("item:359:0");
    check(icon && std::string(icon->name) == "shears" && icon->x == 13 * 16 && icon->y == 5 * 16,
          "Inventory sprite from ADD_ICON(5, 13, L\"shears\")");
    const originalfp::HeldSpriteSpec* held = nullptr;
    for (const auto& spec : originalfp::kHeldSprites)
        if (spec.itemId == Shears && spec.aux == 0) held = &spec;
    check(held && !held->handEquipped && !held->mirrored, "Held flat sprite; ShearsItem is not handEquipped/mirrored");
}

void speedAndHarvest() {
    using mcgame::canHarvest;
    using mcgame::destroyProgress;
    // ShearsItem::getDestroySpeed: web/leaves 15, wool 5, otherwise Item default 1.
    check(near(destroyProgress(Shears, Leaves, 0, true, false), 15.0f / .2f / 30.0f), "Shears on leaves: speed 15");
    check(near(destroyProgress(Shears, Web, 0, true, false), 15.0f / 4.0f / 30.0f), "Shears on web: speed 15");
    check(near(destroyProgress(Shears, Wool, 0, true, false), 5.0f / .8f / 30.0f), "Shears on wool: speed 5");
    check(near(destroyProgress(Shears, Dirt, 0, true, false), destroyProgress(0, Dirt, 0, true, false)),
          "Shears on dirt: same as an empty hand");
    check(near(destroyProgress(Shears, Leaves, 0, true, true), 15.0f / 5.0f / .2f / 30.0f), "Water penalty still applies");
    check(near(destroyProgress(0, Leaves, 0, true, false), 1.0f / .2f / 30.0f), "Hand on leaves unchanged");
    // ShearsItem::canDestroySpecial: web, redstone dust, tripwire.
    check(canHarvest(Shears, Web) && canHarvest(Shears, RedstoneDust) && canHarvest(Shears, TripWire),
          "Shears canDestroySpecial set");
    check(!canHarvest(Shears, Stone) && !canHarvest(Shears, 14), "Shears cannot harvest stone or ore");
    check(canHarvest(Shears, Dirt) && canHarvest(Shears, Leaves), "Always-destroyable materials stay harvestable");
}

void wear() {
    using mcgame::miningWear;
    // ShearsItem::mineBlock: hurtAndBreak(1) on these five tiles, else Item::mineBlock (no wear).
    for (int tile : {Leaves, Web, TallGrass, Vine, TripWire})
        check(miningWear(Shears, tile) == 1, "Shears wear 1 on " + std::to_string(tile));
    for (int tile : {Stone, Dirt, Wool, Flower, RedstoneDust, 32})
        check(miningWear(Shears, tile) == 0, "No shears wear on " + std::to_string(tile));

    // Durability goes through the same Inventory slot damage as every other tool.
    char pattern[] = "/tmp/mcshears-test-XXXXXX";
    const char* path = ::mkdtemp(pattern);
    check(path != nullptr, "temp dir");
    {
        mcworld::World world;
        world.create(4521, path, false);
        mcinventory::Inventory inv;
        inv.open(world);
        check(inv.setGameMode(mcinventory::GameMode::Survival), "survival mode");
        check(inv.addItem(Shears, 0, 1), "Shears enter the inventory");
        check(inv.select(0) && inv.selectedItemId() == Shears && inv.selectedMaxDamage() == 238, "Shears max damage 238");
        for (int i = 0; i < 238; ++i) check(inv.damageSelected(miningWear(Shears, Leaves)), "wear step");
        check(inv.selectedItemId() == Shears && inv.selectedDamage() == 238, "238 uses: still intact at max damage");
        check(inv.damageSelected(miningWear(Shears, TallGrass)) && inv.selectedItemId() != Shears,
              "239th use breaks the shears (ItemInstance::hurt uses >)");
    }
    std::error_code error;
    std::filesystem::remove_all(path, error);
}
} // namespace

int main() {
    catalog();
    recipe();
    sprites();
    speedAndHarvest();
    wear();
    std::printf("PASS shears: catalog row 195 (item:359:0, stack 1, durability 238, creative), diagonal iron recipe, "
                "inventory+held sprite, destroy speed, canDestroySpecial and mineBlock wear (%d checks)\n", checks);
    return 0;
}
