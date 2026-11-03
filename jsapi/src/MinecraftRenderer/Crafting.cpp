#include "Crafting.hpp"

#include "Recipes.hpp"

#include <cstdio>
#include <cstdlib>

namespace crafting {
namespace {

// Parsed grid slot. `present` is false for an empty slot.
struct Slot {
    bool present = false;
    int itemId = -1;
    int aux = 0;
};

// Parse "item:<itemId>:<aux>" into a slot. An empty string is an empty slot;
// anything malformed is treated as empty so a bad key can never match a recipe.
Slot parseKey(const std::string& key) {
    Slot slot;
    if (key.empty()) return slot;
    if (key.compare(0, 5, "item:") != 0) return slot;
    const std::size_t separator = key.find(':', 5);
    if (separator == std::string::npos || separator + 1 >= key.size()) return slot;
    const std::string id = key.substr(5, separator - 5);
    const std::string aux = key.substr(separator + 1);
    if (id.empty() || aux.empty()) return slot;
    // Hand-rolled parsing on purpose: strtol/std::stoi pull in
    // __isoc23_strtol@GLIBC_2.38, which the device's glibc 2.36 cannot resolve,
    // and the whole library then fails to dlopen.
    const auto digits = [](const std::string& text, int& out) {
        if (text.empty() || text.size() > 9) return false;
        int value = 0;
        for (const char c : text) {
            if (c < '0' || c > '9') return false;
            value = value * 10 + (c - '0');
        }
        out = value;
        return true;
    };
    int parsedId = 0, parsedAux = 0;
    if (!digits(id, parsedId) || !digits(aux, parsedAux)) return slot;
    slot.present = true;
    slot.itemId = parsedId;
    slot.aux = parsedAux;
    return slot;
}

// Mirrors ShapedRecipy.cpp:74: an ANY_AUX ingredient accepts any aux value.
bool auxAccepts(int expectedAux, int actualAux) {
    return expectedAux == sourcerecipes::kAnyAux || expectedAux == actualAux;
}

// One offset/mirror attempt, mirroring ShapedRecipy::matches(craftSlots, xOffs,
// yOffs, xFlip) at ShapedRecipy.cpp:49-81. Every one of the nine grid slots is
// compared, so a slot outside the recipe rectangle must be empty; that is what
// rejects a surplus ingredient.
bool matchesAt(const sourcerecipes::RecipeSpec& spec, const Slot* slots,
               int xOffs, int yOffs, bool xFlip) {
    const sourcerecipes::RecipeCell* cells = sourcerecipes::recipeCells(spec);
    for (int x = 0; x < kGridWidth; ++x) {
        for (int y = 0; y < kGridHeight; ++y) {
            const int xs = x - xOffs;
            const int ys = y - yOffs;
            const sourcerecipes::RecipeCell* expected = nullptr;
            if (xs >= 0 && ys >= 0 && xs < spec.width && ys < spec.height) {
                const int index = xFlip ? (spec.width - xs - 1) + ys * spec.width
                                        : xs + ys * spec.width;
                // itemId == -1 encodes a hole in the recipe shape itself.
                if (cells[index].itemId >= 0) expected = &cells[index];
            }
            const Slot& slot = slots[x + y * kGridWidth];
            // Empty slot against empty shape cell: fine. One empty and one not:
            // reject. Only when both are present do we compare id and aux.
            if (expected == nullptr) {
                if (slot.present) return false;
                continue;
            }
            if (!slot.present) return false;
            if (expected->itemId != slot.itemId) return false;
            if (!auxAccepts(expected->aux, slot.aux)) return false;
        }
    }
    return true;
}

// Mirrors ShapedRecipy::matches(craftSlots, level) at ShapedRecipy.cpp:36-47.
bool matchesGrid(const sourcerecipes::RecipeSpec& spec, const Slot* slots) {
    for (int xOffs = 0; xOffs <= kGridWidth - spec.width; ++xOffs) {
        for (int yOffs = 0; yOffs <= kGridHeight - spec.height; ++yOffs) {
            if (matchesAt(spec, slots, xOffs, yOffs, true)) return true;
            if (matchesAt(spec, slots, xOffs, yOffs, false)) return true;
        }
    }
    return false;
}

std::string cellKey(const sourcerecipes::RecipeCell& cell) {
    if (cell.itemId < 0) return std::string();
    char buffer[48];
    if (cell.aux == sourcerecipes::kAnyAux) {
        std::snprintf(buffer, sizeof(buffer), "item:%d:*", cell.itemId);
    } else {
        std::snprintf(buffer, sizeof(buffer), "item:%d:%d", cell.itemId, cell.aux);
    }
    return std::string(buffer);
}

Recipe toRecipe(int index, const sourcerecipes::RecipeSpec& spec) {
    Recipe recipe;
    recipe.index = index;
    recipe.productKey = spec.productKey;
    recipe.productName = spec.productName;
    recipe.group = spec.group;
    recipe.origin = spec.origin;
    recipe.productItemId = spec.productItemId;
    recipe.productAux = spec.productAux;
    recipe.count = spec.productCount;
    recipe.width = spec.width;
    recipe.height = spec.height;
    const sourcerecipes::RecipeCell* cells = sourcerecipes::recipeCells(spec);
    recipe.cells.reserve(static_cast<std::size_t>(spec.width) * spec.height);
    for (int i = 0; i < spec.width * spec.height; ++i) {
        Cell cell;
        cell.empty = cells[i].itemId < 0;
        cell.itemId = cells[i].itemId;
        cell.aux = cells[i].aux;
        cell.key = cellKey(cells[i]);
        recipe.cells.push_back(cell);
    }
    return recipe;
}

}  // namespace

int recipeCount() { return sourcerecipes::recipeCount(); }

Match match(const std::string* keys, int count) {
    Match result;
    if (keys == nullptr || count != kGridSize) return result;
    Slot slots[kGridSize];
    bool any = false;
    for (int i = 0; i < kGridSize; ++i) {
        slots[i] = parseKey(keys[i]);
        any = any || slots[i].present;
    }
    // An empty grid must never match: every recipe has at least one ingredient,
    // and the generator rejects an ingredient-free shape outright.
    if (!any) return result;
    for (int i = 0; i < sourcerecipes::recipeCount(); ++i) {
        const sourcerecipes::RecipeSpec& spec = sourcerecipes::recipes()[i];
        if (!matchesGrid(spec, slots)) continue;
        result.matched = true;
        result.productKey = spec.productKey;
        result.productName = spec.productName;
        result.group = spec.group;
        result.origin = spec.origin;
        result.productItemId = spec.productItemId;
        result.productAux = spec.productAux;
        result.count = spec.productCount;
        result.recipeIndex = i;
        return result;
    }
    return result;
}

Match match(const Grid& grid) {
    if (grid.size() != static_cast<std::size_t>(kGridSize)) return Match();
    return match(grid.data(), static_cast<int>(grid.size()));
}

Recipe describe(int index) {
    const sourcerecipes::RecipeSpec* spec = sourcerecipes::recipeAt(index);
    if (spec == nullptr) return Recipe();
    return toRecipe(index, *spec);
}

std::vector<Recipe> recipesFor(const std::string& productKey) {
    std::vector<Recipe> found;
    if (productKey.empty()) return found;
    for (int i = 0; i < sourcerecipes::recipeCount(); ++i) {
        const sourcerecipes::RecipeSpec& spec = sourcerecipes::recipes()[i];
        if (productKey == spec.productKey) found.push_back(toRecipe(i, spec));
    }
    return found;
}

std::vector<Recipe> allRecipes() {
    std::vector<Recipe> all;
    all.reserve(static_cast<std::size_t>(sourcerecipes::recipeCount()));
    for (int i = 0; i < sourcerecipes::recipeCount(); ++i) {
        all.push_back(toRecipe(i, sourcerecipes::recipes()[i]));
    }
    return all;
}

}  // namespace crafting
