#pragma once
//
// Pure functional shaped-recipe matching over the generated recipe table.
//
// This header is deliberately standalone: it never includes, reads or mutates
// Inventory, World or any renderer state. You pass a 3x3 grid of item keys in
// and get a product key plus a count out. Consuming ingredients, moving stacks
// and drawing a crafting screen are all the caller's business.
//
// Semantics are ported from the pinned upstream matcher:
//   * ShapedRecipy::matches scans every x/y offset that fits the recipe inside
//     the 3x3 grid and tries both the plain and the horizontally mirrored
//     layout (ShapedRecipy.cpp:38-47, :58-59).
//   * An ingredient whose aux is ANY_AUX_VALUE (-1) matches any aux value
//     (Recipes.h:69, ShapedRecipy.cpp:74). Item*/Tile* ingredients are built
//     with that sentinel (Recipes.cpp:1111, :1116).
//   * A grid slot outside the recipe rectangle must be EMPTY, which is what
//     makes a surplus ingredient fail to match (ShapedRecipy.cpp:66-69).
//
// NOT IMPLEMENTED, and not claimed anywhere in this module:
//   * Smelting. FurnaceRecipes.cpp is a separate singleton and was skipped this
//     round, so iron and gold ingots have no survival source and every
//     iron/gold recipe is creative-only.
//   * Shapeless recipes, so dyes, most food and book/eye-of-ender are absent.
//   * Durability consumption, mining speed, harvest-level gating, enchanting,
//     armor damage reduction, sword combat bonuses, hoe tilling, shears
//     shearing. Those need a real Item engine; Slot has no damage field and
//     World::mine is creative-instant.
//   * Tool repair by combining two damaged copies, which needs that same
//     damage field.
//   * Ingredient consumption and any crafting GUI. Matching works as a backend
//     capability, but a player cannot craft from the interface yet.
//
#include <string>
#include <vector>

namespace crafting {

// A 3x3 crafting grid, row major, index = x + y * 3. An empty string is an
// empty slot. Keys use the item-catalog format "item:<itemId>:<aux>".
constexpr int kGridWidth = 3;
constexpr int kGridHeight = 3;
constexpr int kGridSize = kGridWidth * kGridHeight;

using Grid = std::vector<std::string>;

// Result of a match. `matched` is false and the other fields are empty/zero
// when the grid matches no recipe.
struct Match {
    bool matched = false;
    std::string productKey;   // "item:<itemId>:<aux>", or empty when unmatched
    std::string productName;  // Display name from the item catalog
    std::string group;        // Upstream recipe group: tool, armour, structure, ...
    std::string origin;       // Source file:line the recipe was extracted from
    int productItemId = -1;
    int productAux = 0;
    int count = 0;            // Units produced per craft
    int recipeIndex = -1;     // Index into the generated recipe table
};

// One cell of a recipe shape, as reported by describe().
struct Cell {
    bool empty = true;   // true when this slot must be left empty
    int itemId = -1;
    int aux = -1;        // -1 means any aux value matches
    std::string key;     // "item:<id>:<aux>" or "item:<id>:*", empty if empty
};

// A recipe shape plus its product, for a recipe book or a test.
struct Recipe {
    int index = -1;
    std::string productKey;
    std::string productName;
    std::string group;
    std::string origin;
    int productItemId = -1;
    int productAux = 0;
    int count = 0;
    int width = 0;
    int height = 0;
    std::vector<Cell> cells;  // width*height, row major
};

// Total number of recipes in the generated table.
int recipeCount();

// Match a 3x3 grid against every recipe. The grid must hold exactly nine
// entries; a grid of the wrong size, or an entirely empty grid, never matches.
// Offsets and horizontal mirroring are applied, matching upstream.
Match match(const Grid& grid);

// Convenience overload for a nine-element C array of keys.
Match match(const std::string* keys, int count);

// Describe a recipe by table index. Returns a Recipe with index == -1 when the
// index is out of range.
Recipe describe(int index);

// Every recipe producing this product key, for a future recipe book. Returns an
// empty vector for an unknown key.
std::vector<Recipe> recipesFor(const std::string& productKey);

// Every recipe in table order.
std::vector<Recipe> allRecipes();

}  // namespace crafting
