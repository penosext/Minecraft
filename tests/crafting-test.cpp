// Standalone host test for the pure shaped-recipe matcher. Link Crafting.cpp
// only; this test touches no World, Inventory or renderer state.
//
// It verifies what the module actually claims and nothing more: recipe data
// fidelity against the pinned upstream shapes, offset and mirror matching,
// ANY_AUX ingredient behavior, rejection of surplus ingredients and of the
// empty grid, and that every product key exists in the item catalog.
//
// It deliberately does NOT test durability, mining speed, harvest level,
// enchanting, armor reduction, sword bonuses, hoe tilling, shears shearing,
// smelting or ingredient consumption, because none of those are implemented.
#include "Crafting.hpp"
#include "ItemCatalog.hpp"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

int checks = 0;

void check(bool condition, const std::string& what) {
    ++checks;
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    std::abort();
}

// Build a 3x3 grid from three row strings plus a legend. A space is an empty
// slot; any other character is looked up in the legend.
crafting::Grid grid(const std::string& row0, const std::string& row1, const std::string& row2,
                    const std::map<char, std::string>& legend) {
    const std::string rows[3] = {row0, row1, row2};
    crafting::Grid out(crafting::kGridSize);
    for (int y = 0; y < 3; ++y) {
        check(rows[y].size() == 3, "test grid rows must be three characters wide");
        for (int x = 0; x < 3; ++x) {
            const char ch = rows[y][x];
            if (ch == ' ') continue;
            const auto it = legend.find(ch);
            check(it != legend.end(), std::string("test legend is missing '") + ch + "'");
            out[x + y * 3] = it->second;
        }
    }
    return out;
}

// Ingredient keys. Tools/armor ingredients carry aux 0 in the catalog; recipes
// accept any aux, which the ANY_AUX test below exercises directly.
const std::string kStick = "item:280:0";
const std::string kPlank = "item:5:0";
const std::string kCobble = "item:4:0";
const std::string kIron = "item:265:0";
const std::string kDiamond = "item:264:0";
const std::string kGold = "item:266:0";
const std::string kLeather = "item:334:0";

std::string materialKey(const std::string& material) {
    if (material == "wood") return kPlank;
    if (material == "stone") return kCobble;
    if (material == "iron") return kIron;
    if (material == "diamond") return kDiamond;
    if (material == "gold") return kGold;
    check(false, "unknown tool material " + material);
    return std::string();
}

crafting::Match expect(const crafting::Grid& g, const std::string& productKey,
                       int count, const std::string& what) {
    const crafting::Match match = crafting::match(g);
    check(match.matched, what + ": expected a match");
    check(match.productKey == productKey,
          what + ": expected " + productKey + ", got " + match.productKey);
    check(match.count == count,
          what + ": expected count " + std::to_string(count) + ", got " + std::to_string(match.count));
    return match;
}

void expectNoMatch(const crafting::Grid& g, const std::string& what) {
    const crafting::Match match = crafting::match(g);
    check(!match.matched, what + ": expected no match, got " + match.productKey);
}

// ------------------------------------------------------------------ test cases

// Every product key the table emits must exist in the generated item catalog.
// This is the guard against an invented item sneaking into a recipe.
void productKeysExistInCatalog() {
    const std::vector<crafting::Recipe> all = crafting::allRecipes();
    check(!all.empty(), "the recipe table must not be empty");
    check(static_cast<int>(all.size()) == crafting::recipeCount(), "allRecipes size disagrees with recipeCount");
    for (const crafting::Recipe& recipe : all) {
        const sourceitems::ItemSpec* product = sourceitems::findItem(recipe.productKey);
        check(product != nullptr, "product key not in item catalog: " + recipe.productKey);
        check(product->itemId == recipe.productItemId && product->aux == recipe.productAux,
              "product key does not agree with its id/aux: " + recipe.productKey);
        check(recipe.count >= 1, "product count must be positive: " + recipe.productKey);
        check(recipe.width >= 1 && recipe.width <= 3 && recipe.height >= 1 && recipe.height <= 3,
              "recipe shape exceeds the 3x3 grid: " + recipe.productKey);
        check(recipe.cells.size() == static_cast<std::size_t>(recipe.width * recipe.height),
              "recipe cell count disagrees with its shape: " + recipe.productKey);
        bool anyIngredient = false;
        for (const crafting::Cell& cell : recipe.cells) {
            if (cell.empty) continue;
            anyIngredient = true;
            // An ANY_AUX ingredient has no single catalog key, so check the id
            // resolves for at least one aux; a fixed aux must resolve exactly.
            if (cell.aux == -1) {
                bool found = false;
                for (int i = 0; i < sourceitems::kItemCount && !found; ++i) {
                    found = sourceitems::kItems[i].itemId == cell.itemId;
                }
                check(found, "ANY_AUX ingredient id not in item catalog: " + cell.key);
            } else {
                check(sourceitems::itemIndexForIdAux(cell.itemId, cell.aux) >= 0,
                      "ingredient not in item catalog: " + cell.key);
            }
        }
        check(anyIngredient, "recipe has no ingredient: " + recipe.productKey);
        check(!recipe.origin.empty(), "recipe lacks source provenance: " + recipe.productKey);
    }
}

// 5 materials x 4 tool types. Shapes are transcribed from ToolRecipies.cpp:9-26,
// whose order is pickaxe, shovel, axe, hoe.
void toolRecipes() {
    struct Row { const char* material; int pickaxe, shovel, hatchet, hoe; };
    // Item IDs from Item.h, independently transcribed rather than read back.
    const std::vector<Row> rows = {
        {"wood", 270, 269, 271, 290},
        {"stone", 274, 273, 275, 291},
        {"iron", 257, 256, 258, 292},
        {"diamond", 278, 277, 279, 293},
        {"gold", 285, 284, 286, 294},
    };
    int covered = 0;
    for (const Row& row : rows) {
        const std::map<char, std::string> legend = {{'X', materialKey(row.material)}, {'#', kStick}};
        const std::string material = row.material;
        // Pickaxe: XXX / " # " / " # "
        expect(grid("XXX", " # ", " # ", legend), "item:" + std::to_string(row.pickaxe) + ":0", 1,
               material + " pickaxe");
        // Shovel: X / # / #   (placed in the left column)
        expect(grid("X  ", "#  ", "#  ", legend), "item:" + std::to_string(row.shovel) + ":0", 1,
               material + " shovel");
        // Axe: XX / X# / " #"
        expect(grid("XX ", "X# ", " # ", legend), "item:" + std::to_string(row.hatchet) + ":0", 1,
               material + " axe");
        // Hoe: XX / " #" / " #"
        expect(grid("XX ", " # ", " # ", legend), "item:" + std::to_string(row.hoe) + ":0", 1,
               material + " hoe");
        covered += 4;
    }
    check(covered == 20, "expected 20 tool recipes, covered " + std::to_string(covered));
}

// 5 swords from WeaponRecipies.cpp:10-15, shape X / X / #. Bow and arrow are
// commented out of that file and are correctly absent here.
void swordRecipes() {
    const std::vector<std::pair<const char*, int>> swords = {
        {"wood", 268}, {"stone", 272}, {"iron", 267}, {"diamond", 276}, {"gold", 283}};
    for (const auto& sword : swords) {
        const std::map<char, std::string> legend = {{'X', materialKey(sword.first)}, {'#', kStick}};
        expect(grid("X  ", "X  ", "#  ", legend), "item:" + std::to_string(sword.second) + ":0", 1,
               std::string(sword.first) + " sword");
    }
    check(swords.size() == 5, "expected 5 sword recipes");
}

// 4 pieces x 4 craftable materials. Shapes from ArmorRecipes.cpp:13-28.
// Chain is NOT here: ArmorRecipes.cpp:45-71 comments its table rows out.
void armorRecipes() {
    struct Row { const char* material; const char* key; int helmet, chestplate, leggings, boots; };
    const std::vector<Row> rows = {
        {"leather", kLeather.c_str(), 298, 299, 300, 301},
        {"iron", kIron.c_str(), 306, 307, 308, 309},
        {"diamond", kDiamond.c_str(), 310, 311, 312, 313},
        {"gold", kGold.c_str(), 314, 315, 316, 317},
    };
    int covered = 0;
    for (const Row& row : rows) {
        const std::map<char, std::string> legend = {{'X', row.key}};
        const std::string material = row.material;
        expect(grid("XXX", "X X", "   ", legend), "item:" + std::to_string(row.helmet) + ":0", 1,
               material + " helmet");
        expect(grid("X X", "XXX", "XXX", legend), "item:" + std::to_string(row.chestplate) + ":0", 1,
               material + " chestplate");
        expect(grid("XXX", "X X", "X X", legend), "item:" + std::to_string(row.leggings) + ":0", 1,
               material + " leggings");
        expect(grid("X X", "X X", "   ", legend), "item:" + std::to_string(row.boots) + ":0", 1,
               material + " boots");
        covered += 4;
    }
    check(covered == 16, "expected 16 armor recipes, covered " + std::to_string(covered));
}

// Chain armor must stay uncraftable while the four chain items still exist.
void chainArmorHasNoRecipe() {
    for (int id : {302, 303, 304, 305}) {
        const std::string key = "item:" + std::to_string(id) + ":0";
        check(sourceitems::findItem(key) != nullptr, "chain armor item should exist: " + key);
        check(crafting::recipesFor(key).empty(), "chain armor must have no recipe: " + key);
    }
}

// StructureRecipies.cpp:52-58 (workbench) and :60 (furnace).
void structureRecipes() {
    const std::map<char, std::string> plank = {{'#', kPlank}};
    const std::map<char, std::string> cobble = {{'#', kCobble}};
    expect(grid("## ", "## ", "   ", plank), "item:58:0", 1, "workbench");
    expect(grid("###", "# #", "###", cobble), "item:61:0", 1, "furnace");
}

// ShapedRecipy.cpp:38-47 scans every offset that fits the shape in the grid.
void offsetMatching() {
    const std::map<char, std::string> plank = {{'#', kPlank}};
    // The 2x2 workbench fits at four offsets; all four must match.
    expect(grid("## ", "## ", "   ", plank), "item:58:0", 1, "workbench top left");
    expect(grid(" ##", " ##", "   ", plank), "item:58:0", 1, "workbench top right");
    expect(grid("   ", "## ", "## ", plank), "item:58:0", 1, "workbench bottom left");
    expect(grid("   ", " ##", " ##", plank), "item:58:0", 1, "workbench bottom right");
    // A 2x2 block split across a gap is not a 2x2 square and must not match.
    expectNoMatch(grid("# #", "# #", "   ", plank), "split workbench columns");
    expectNoMatch(grid("## ", "   ", "## ", plank), "split workbench rows");
    // The stick recipe (plank over plank, Recipes.cpp:83) at every offset.
    for (int x = 0; x < 3; ++x) {
        for (int y = 0; y < 2; ++y) {
            crafting::Grid g(crafting::kGridSize);
            g[x + y * 3] = kPlank;
            g[x + (y + 1) * 3] = kPlank;
            expect(g, "item:280:0", 4, "sticks at offset " + std::to_string(x) + "," + std::to_string(y));
        }
    }
}

// ShapedRecipy.cpp:58-59 also tries the horizontally mirrored layout.
void mirrorMatching() {
    const std::map<char, std::string> legend = {{'X', materialKey("wood")}, {'#', kStick}};
    // The axe is asymmetric: XX / X# / " #". Its mirror XX / #X / "# " must also
    // match, which is the whole point of the xFlip pass.
    expect(grid("XX ", "X# ", " # ", legend), "item:271:0", 1, "wooden axe, source layout");
    expect(grid("XX ", "#X ", "#  ", legend), "item:271:0", 1, "wooden axe, mirrored");
    // Mirrored and offset together.
    expect(grid(" XX", " #X", " # ", legend), "item:271:0", 1, "wooden axe, mirrored and shifted");
    // Vertical flipping is NOT part of upstream matching and must not match.
    expectNoMatch(grid(" # ", "X# ", "XX ", legend), "axe flipped vertically");
    // The wooden stairs shape (Recipes.cpp:199) mirrors too.
    const std::map<char, std::string> plank = {{'#', kPlank}};
    expect(grid("#  ", "## ", "###", plank), "item:53:0", 4, "wooden stairs, source layout");
    expect(grid("  #", " ##", "###", plank), "item:53:0", 4, "wooden stairs, mirrored");
}

// An Item*/Tile* ingredient is built with ANY_AUX_VALUE (Recipes.cpp:1111,
// :1116), so any aux of that item id satisfies it.
void anyAuxIngredients() {
    // The stick recipe takes Tile::wood with any aux, so all four plank colors
    // work, and mixing colors still works because each cell matches on its own.
    for (int aux = 0; aux < 4; ++aux) {
        const std::string plank = "item:5:" + std::to_string(aux);
        check(sourceitems::findItem(plank) != nullptr, "plank variant should exist: " + plank);
        crafting::Grid g(crafting::kGridSize);
        g[0] = plank;
        g[3] = plank;
        expect(g, "item:280:0", 4, "sticks from plank aux " + std::to_string(aux));
    }
    crafting::Grid mixed(crafting::kGridSize);
    mixed[0] = "item:5:0";
    mixed[3] = "item:5:3";
    expect(mixed, "item:280:0", 4, "sticks from mixed plank colors");
    // By contrast, the plank recipes pin a specific log aux (Recipes.cpp:54-81),
    // so log aux 1 must yield plank aux 1 and never plank aux 0.
    for (int aux = 0; aux < 4; ++aux) {
        crafting::Grid g(crafting::kGridSize);
        g[4] = "item:17:" + std::to_string(aux);
        expect(g, "item:5:" + std::to_string(aux), 4, "planks from log aux " + std::to_string(aux));
    }
    // A fixed-aux ingredient must reject the wrong aux. Spruce stairs want
    // plank aux 1 (Recipes.cpp:269), so oak planks must not produce them.
    const std::map<char, std::string> oak = {{'#', "item:5:0"}};
    const crafting::Match oakStairs = crafting::match(grid("#  ", "## ", "###", oak));
    check(oakStairs.matched && oakStairs.productKey == "item:53:0",
          "oak stairs shape should yield oak stairs");
    const std::map<char, std::string> spruce = {{'#', "item:5:1"}};
    const crafting::Match spruceStairs = crafting::match(grid("#  ", "## ", "###", spruce));
    check(spruceStairs.matched && spruceStairs.productKey == "item:134:0",
          "spruce plank stairs shape should yield spruce stairs, got " + spruceStairs.productKey);
}

// A slot outside the recipe rectangle must be empty (ShapedRecipy.cpp:66-69).
void surplusIngredientsRejected() {
    const std::map<char, std::string> plank = {{'#', kPlank}};
    // One extra plank beside a valid 2x2 workbench breaks the match.
    expectNoMatch(grid("###", "## ", "   ", plank), "workbench plus one extra plank");
    expectNoMatch(grid("## ", "## ", "#  ", plank), "workbench plus a plank below");
    expectNoMatch(grid("###", "###", "###", plank), "a full grid of planks");
    // A valid pickaxe with a stray stick in a corner must not match.
    const std::map<char, std::string> legend = {{'X', kPlank}, {'#', kStick}};
    expect(grid("XXX", " # ", " # ", legend), "item:270:0", 1, "clean wooden pickaxe");
    expectNoMatch(grid("XXX", "## ", " # ", legend), "wooden pickaxe plus a stray stick");
    // A wrong ingredient in an otherwise valid shape must not match.
    const std::map<char, std::string> wrong = {{'X', kPlank}, {'#', kIron}};
    expectNoMatch(grid("XXX", " # ", " # ", wrong), "pickaxe shape with iron instead of sticks");
}

// An empty grid, and malformed input, must never match.
void emptyAndMalformedGrid() {
    expectNoMatch(crafting::Grid(crafting::kGridSize), "a fully empty grid");
    // Wrong grid size.
    const crafting::Match small = crafting::match(crafting::Grid(4));
    check(!small.matched, "a four-slot grid must not match");
    const crafting::Match large = crafting::match(crafting::Grid(16));
    check(!large.matched, "a sixteen-slot grid must not match");
    const crafting::Match nullGrid = crafting::match(nullptr, crafting::kGridSize);
    check(!nullGrid.matched, "a null grid must not match");
    // Malformed keys are treated as empty slots, so they cannot craft anything.
    crafting::Grid junk(crafting::kGridSize);
    junk[0] = "not-an-item";
    junk[1] = "item:";
    junk[2] = "item:abc:0";
    junk[3] = "item:5";
    junk[4] = "item:5:xyz";
    expectNoMatch(junk, "a grid of malformed keys");
    // A malformed key next to a valid half-recipe must not complete it.
    crafting::Grid partial(crafting::kGridSize);
    partial[0] = kPlank;
    partial[3] = "item:5:oops";
    expectNoMatch(partial, "one plank plus one malformed key");
}

// describe()/recipesFor() are the recipe-book facing lookups.
void reverseLookup() {
    const std::vector<crafting::Recipe> pickaxe = crafting::recipesFor("item:270:0");
    check(pickaxe.size() == 1, "the wooden pickaxe should have exactly one recipe");
    check(pickaxe[0].width == 3 && pickaxe[0].height == 3, "the pickaxe shape is 3x3");
    check(pickaxe[0].cells.size() == 9, "the pickaxe shape has nine cells");
    // XXX / " # " / " # ": the head row is full, the handle runs down the
    // middle column, and the four cells beside the handle are holes.
    const std::vector<crafting::Cell>& cells = pickaxe[0].cells;
    check(!cells[0].empty && !cells[1].empty && !cells[2].empty, "the pickaxe head row is full");
    check(cells[3].empty && cells[5].empty && cells[6].empty && cells[8].empty,
          "the pickaxe handle column leaves its sides empty");
    check(!cells[4].empty && !cells[7].empty, "the pickaxe handle runs down the middle");
    check(cells[4].key == "item:280:*", "the pickaxe handle is an ANY_AUX stick, got " + cells[4].key);
    check(cells[0].key == "item:5:*", "the wooden pickaxe head is ANY_AUX planks, got " + cells[0].key);
    // Round trip describe() against the match result.
    const std::map<char, std::string> legend = {{'X', kPlank}, {'#', kStick}};
    const crafting::Match match = expect(grid("XXX", " # ", " # ", legend), "item:270:0", 1,
                                         "wooden pickaxe for round trip");
    const crafting::Recipe described = crafting::describe(match.recipeIndex);
    check(described.index == match.recipeIndex, "describe should round trip the matched index");
    check(described.productKey == match.productKey, "describe should round trip the product key");
    // Out of range and unknown lookups fail cleanly.
    check(crafting::describe(-1).index == -1, "describe(-1) should fail cleanly");
    check(crafting::describe(crafting::recipeCount()).index == -1, "describe(count) should fail cleanly");
    check(crafting::recipesFor("item:999999:0").empty(), "an unknown product has no recipes");
    check(crafting::recipesFor("").empty(), "an empty key has no recipes");
}

// Matching must be a pure function: same grid in, same answer out, with no
// dependence on call order.
void matchingIsPure() {
    const std::map<char, std::string> legend = {{'X', kIron}, {'#', kStick}};
    const crafting::Grid g = grid("XXX", " # ", " # ", legend);
    const crafting::Match first = crafting::match(g);
    for (int i = 0; i < 16; ++i) {
        // Interleave unrelated queries to catch hidden state.
        crafting::match(crafting::Grid(crafting::kGridSize));
        crafting::recipesFor("item:58:0");
        const crafting::Match again = crafting::match(g);
        check(again.matched == first.matched && again.productKey == first.productKey &&
              again.count == first.count && again.recipeIndex == first.recipeIndex,
              "repeated matching must return an identical result");
    }
    // The input grid must not be mutated.
    check(g == grid("XXX", " # ", " # ", legend), "match must not mutate its input grid");
}

// Iron and gold recipes exist but are survival-unreachable because smelting is
// not implemented. The test asserts the honest boundary rather than hiding it.
void ironAndGoldAreCreativeOnly() {
    // Iron and gold ingots have no shaped recipe here: upstream makes them by
    // smelting (FurnaceRecipes.cpp:20-21), which this round does not implement.
    // The ingot<->block ore recipes are also absent because the blocks are not
    // in the item catalog, so there is no shaped path to an ingot at all.
    check(crafting::recipesFor("item:265:0").empty(),
          "iron ingots must have no shaped recipe; they are smelting only");
    check(crafting::recipesFor("item:266:0").empty(),
          "gold ingots must have no shaped recipe; they are smelting only");
    // Their gear still has recipes, reachable only once ingots are given.
    check(!crafting::recipesFor("item:257:0").empty(), "the iron pickaxe should still have a recipe");
    check(!crafting::recipesFor("item:314:0").empty(), "the gold helmet should still have a recipe");
    // Diamonds drop directly from ore, so diamond gear is genuinely reachable.
    check(!crafting::recipesFor("item:278:0").empty(), "the diamond pickaxe should have a recipe");
}

// The table must not contain two identical shapes for the same product, which
// would make matching order dependent.
void noDuplicateShapes() {
    std::set<std::string> signatures;
    for (const crafting::Recipe& recipe : crafting::allRecipes()) {
        std::string signature = recipe.productKey + "|" + std::to_string(recipe.count) + "|" +
                                std::to_string(recipe.width) + "x" + std::to_string(recipe.height);
        for (const crafting::Cell& cell : recipe.cells) signature += "|" + cell.key;
        check(signatures.insert(signature).second, "duplicate recipe shape: " + signature);
    }
}

// Audited totals for this round. These are transcribed expectations, so a
// drifting generator cannot quietly agree with itself.
void auditedCounts() {
    std::map<std::string, int> byGroup;
    int tools = 0, swords = 0, armor = 0;
    for (const crafting::Recipe& recipe : crafting::allRecipes()) {
        ++byGroup[recipe.group];
        const int id = recipe.productItemId;
        if (id == 268 || id == 272 || id == 267 || id == 276 || id == 283) ++swords;
        if (id >= 298 && id <= 317) ++armor;
    }
    // Count tools by their exact IDs so the arithmetic cannot drift.
    for (int id : {270, 269, 271, 290, 274, 273, 275, 291, 257, 256, 258, 292,
                   278, 277, 279, 293, 285, 284, 286, 294}) {
        const int found = static_cast<int>(crafting::recipesFor("item:" + std::to_string(id) + ":0").size());
        check(found == 1, "tool " + std::to_string(id) + " should have exactly one recipe, found " +
                          std::to_string(found));
        ++tools;
    }
    check(tools == 20, "expected 20 tool recipes");
    check(swords == 5, "expected 5 sword recipes, counted " + std::to_string(swords));
    check(armor == 16, "expected 16 armor recipes (chain excluded), counted " + std::to_string(armor));
    check(byGroup["armour"] == 16, "the armour group should hold 16 recipes");
    std::printf("recipe table: %d recipes", crafting::recipeCount());
    for (const auto& group : byGroup) std::printf(", %s %d", group.first.c_str(), group.second);
    std::printf("\n");
}

}  // namespace

int main() {
    productKeysExistInCatalog();
    toolRecipes();
    swordRecipes();
    armorRecipes();
    chainArmorHasNoRecipe();
    structureRecipes();
    offsetMatching();
    mirrorMatching();
    anyAuxIngredients();
    surplusIngredientsRejected();
    emptyAndMalformedGrid();
    reverseLookup();
    matchingIsPure();
    ironAndGoldAreCreativeOnly();
    noDuplicateShapes();
    auditedCounts();
    std::printf("PASS: %d checks over 20 tool, 5 sword, 16 armor, workbench/furnace recipes, "
                "offset/mirror matching, ANY_AUX ingredients, surplus and empty rejection, "
                "catalog-backed product identity\n", checks);
    std::printf("NOT IMPLEMENTED (not tested because not claimed): smelting, shapeless recipes, "
                "durability, mining speed, harvest level, enchanting, armor reduction, sword "
                "bonuses, hoe tilling, shears shearing, ingredient consumption, crafting GUI\n");
    return 0;
}
