#include "ToolRules.hpp"

#include "ItemCatalog.hpp"
#include "LegacyCatalog.hpp"

#include <algorithm>
#include <cstring>

namespace mcgame {
namespace {
// Source Tile constructor hardness values (Tile.cpp destroy-speed arguments).
// Unlisted tiles are deliberately absent so callers can refuse rather than
// invent a hardness. These are hardness only; harvest level is separate.
struct Hardness { int tileId; float hardness; };
constexpr Hardness kHardness[] = {
    {1, 1.5f}, {2, .6f}, {3, .5f}, {4, 2}, {5, 2}, {7, -1}, {12, .5f}, {13, .6f},
    {14, 3}, {15, 3}, {16, 3}, {17, 2}, {18, .2f}, {19, .6f}, {20, .3f}, {21, 3},
    {22, 3}, {23, 3.5f}, {24, .8f}, {25, .8f}, {26, .2f}, {27, .7f}, {28, .7f},
    {29, .5f}, {30, 4}, {31, 0}, {32, 0}, {33, .5f}, {35, .8f}, {37, 0}, {38, 0},
    {39, 0}, {40, 0}, {41, 3}, {42, 5}, {43, 2}, {44, 2}, {45, 2}, {46, 0},
    {47, 1.5f}, {48, 2}, {49, 50}, {50, 0}, {51, 0}, {53, 2}, {54, 2.5f},
    {56, 3}, {57, 5}, {58, 2.5f}, {59, 0}, {60, .6f}, {61, 3.5f}, {62, 3.5f},
    {63, 1}, {64, 3}, {65, .4f}, {66, .7f}, {67, 2}, {68, 1}, {69, .5f},
    {70, .5f}, {71, 5}, {72, .5f}, {73, 3}, {74, 3}, {75, 0}, {76, 0},
    {77, .5f}, {78, .1f}, {79, .5f}, {80, .2f}, {81, .4f}, {82, .6f},
    {83, 0}, {84, 2}, {85, 2}, {86, 1}, {87, .4f}, {88, .5f}, {89, .3f},
    {90, -1}, {91, 1}, {92, .5f}, {93, 0}, {94, 0}, {95, .3f}, {96, 3},
    {97, .75f}, {98, 1.5f}, {99, .2f}, {100, .2f}, {101, 5}, {102, .3f},
    {103, 1}, {104, 0}, {105, 0}, {106, .2f}, {107, 2}, {108, 2}, {109, 2},
    {110, .6f}, {111, 0}, {112, 2}, {113, 2}, {114, 2}, {115, 0}, {116, 5},
    {117, .5f}, {118, 2}, {119, -1}, {120, -1}, {121, 3}, {122, 3}, {123, .3f},
    {124, .3f}, {125, 2}, {126, 2}, {127, .2f}, {128, .8f}, {129, 3},
    {130, 22.5f}, {131, 0}, {132, 0}, {133, 5}, {134, 2}, {135, 2}, {136, 2},
    {137, -1}, {138, 3}, {139, 2}, {140, .5f}, {141, 0}, {142, 0}, {143, .5f},
    {144, 1}, {145, 5}, {146, 2.5f}, {147, .5f}, {148, .5f}, {149, 0},
    {150, 0}, {151, .2f}, {152, 5}, {153, 3}, {154, 3}, {155, .8f},
    {156, .8f}, {157, .7f}, {158, 3.5f},
};
// Source PickaxeItem/ShovelItem/HatchetItem/WeaponItem diggable material lists.
constexpr int kPickaxeTiles[] = {1, 4, 5, 14, 15, 16, 21, 22, 24, 41, 42, 43, 44, 45, 47, 48, 49, 56, 57, 61, 62, 67, 73, 74, 87, 98, 108, 109, 112, 113, 114, 116, 121, 122, 125, 126, 128, 129, 133, 134, 135, 136, 139, 145, 146, 152, 153, 155, 156, 158};
constexpr int kShovelTiles[] = {2, 3, 12, 13, 60, 80, 78, 82, 88, 110};
constexpr int kAxeTiles[] = {5, 17, 47, 53, 54, 58, 63, 64, 65, 68, 71, 72, 84, 85, 96, 107, 125, 126, 134, 135, 136, 146, 151, 157};
// Source harvest gating (PickaxeItem::canDestroySpecial / ShovelItem / WeaponItem).
struct Harvest { int tileId; int level; };
constexpr Harvest kHarvest[] = {
    {49, 3}, {56, 2}, {57, 2}, {129, 2}, {133, 2}, {14, 2}, {41, 2}, {73, 2}, {74, 2},
    {15, 1}, {42, 1}, {21, 1}, {22, 1}, {1, 0}, {4, 0}, {43, 0}, {44, 0}, {45, 0},
    {47, 0}, {48, 0}, {24, 0}, {98, 0}, {112, 0}, {113, 0}, {114, 0}, {121, 0},
    {122, 0}, {125, 0}, {126, 0}, {128, 0}, {139, 0}, {145, 0}, {146, 0}, {152, 0},
    {153, 0}, {155, 0}, {156, 0}, {158, 0}, {108, 0}, {109, 0}, {67, 0}, {5, 0},
    {61, 0}, {62, 0}, {116, 0}, {134, 0}, {135, 0}, {136, 0}, {87, 0}, {16, 0},
};
// Source Tile::getResource/resourceCount for the catalog-representable subset.
// Absent tiles are "unknown rule", not "no drop", so survival can refuse.
struct Drop { int tileId; int itemId; int aux; int count; bool needsTool; };
constexpr Drop kDrops[] = {
    {1, 4, 0, 1, true},   // stone -> cobblestone
    {2, 3, 0, 1, false},  // grass -> dirt
    {3, 3, 0, 1, false},
    {4, 4, 0, 1, true},
    {5, 5, -1, 1, false}, // planks keep their material aux
    {12, 12, -1, 1, false},
    {13, 13, 0, 1, false},
    {14, 14, 0, 1, true},
    {15, 15, 0, 1, true},
    {16, 263, 0, 1, true}, // coal ore -> coal
    {17, 17, -1, 1, false},
    {21, 351, 4, 1, true}, // lapis ore -> lapis dye
    {22, 22, 0, 1, true},
    {24, 24, -1, 1, false},
    {35, 35, -1, 1, false},
    {41, 41, 0, 1, true},
    {42, 42, 0, 1, true},
    {45, 45, 0, 1, true},
    {47, 340, 0, 3, false}, // bookshelf -> books
    {48, 48, 0, 1, true},
    {49, 49, 0, 1, true},
    {53, 53, 0, 1, false},
    {54, 54, 0, 1, false},
    {56, 264, 0, 1, true}, // diamond ore -> diamond
    {57, 57, 0, 1, true},
    {58, 58, 0, 1, false},
    {60, 3, 0, 1, false}, // farmland -> dirt
    {61, 61, 0, 1, true},
    {62, 61, 0, 1, true},
    {64, 324, 0, 1, false},
    {67, 67, 0, 1, false},
    {71, 330, 0, 1, true},
    {73, 331, 0, 4, true}, // redstone ore
    {74, 331, 0, 4, true},
    {80, 332, 0, 1, false}, // snow block -> snowball
    {81, 81, 0, 1, false},
    {82, 337, 0, 4, false}, // clay -> clay balls
    {85, 85, 0, 1, false},
    {86, 86, 0, 1, false},
    {87, 87, 0, 1, true},
    {89, 348, 0, 1, true}, // glowstone -> dust
    {91, 91, 0, 1, false},
    {98, 98, -1, 1, true},
    {108, 108, 0, 1, true},
    {109, 109, 0, 1, true},
    {112, 112, 0, 1, true},
    {114, 114, 0, 1, true},
    {121, 121, 0, 1, true},
    {128, 128, 0, 1, true},
    {129, 388, 0, 1, true}, // emerald ore -> emerald
    {133, 133, 0, 1, true},
    {134, 134, 0, 1, false},
    {135, 135, 0, 1, false},
    {136, 136, 0, 1, false},
    {139, 139, -1, 1, true},
    {155, 155, -1, 1, true},
    {156, 156, 0, 1, true},
    {158, 158, 0, 1, true},
    // Known empty drops: no resource in source.
    {7, 0, 0, 0, false},   // bedrock
    {8, 0, 0, 0, false}, {9, 0, 0, 0, false}, {10, 0, 0, 0, false}, {11, 0, 0, 0, false},
    {20, 0, 0, 0, false},  // glass: GlassTile drops nothing without silk touch
    {79, 0, 0, 0, false},  // ice
    {102, 0, 0, 0, false}, // glass pane
};

ToolKind kindOf(const sourceitems::ToolArmorSpec& spec) {
    if (spec.kind == 2) return ToolKind::Armor;
    if (!std::strcmp(spec.sourceClass, "WeaponItem")) return ToolKind::Sword;
    if (!std::strcmp(spec.sourceClass, "ShovelItem")) return ToolKind::Shovel;
    if (!std::strcmp(spec.sourceClass, "PickaxeItem")) return ToolKind::Pickaxe;
    if (!std::strcmp(spec.sourceClass, "HatchetItem")) return ToolKind::Axe;
    if (!std::strcmp(spec.sourceClass, "HoeItem")) return ToolKind::Hoe;
    return ToolKind::None;
}
bool listed(const int* begin, const int* end, int tileId) {
    return std::find(begin, end, tileId) != end;
}
// Source WeaponItem::getDestroySpeed material multipliers for the subset the
// catalog can express: web 15, plant family 1.5, everything else 1.
float swordSpeed(int tileId) {
    if (tileId == 30) return 15;
    switch (tileId) {
        case 6: case 18: case 31: case 32: case 37: case 38: case 39: case 40:
        case 59: case 83: case 103: case 104: case 105: case 106: case 111:
        case 115: case 141: case 142: case 161: case 175:
            return 1.5f;
        default: return 1;
    }
}
} // namespace

const ToolProperties* toolProperties(int itemId) {
    // Immutable, built once; safe for concurrent reads after initialization.
    static const std::vector<ToolProperties> table = [] {
        std::vector<ToolProperties> result;
        result.reserve(sizeof(sourceitems::kToolArmor) / sizeof(sourceitems::kToolArmor[0]));
        for (const auto& spec : sourceitems::kToolArmor) {
            ToolProperties properties;
            properties.itemId = spec.itemId;
            properties.maxDamage = spec.durability;
            properties.armorSlot = spec.armorSlot;
            properties.defense = spec.defense < 0 ? 0 : spec.defense;
            properties.tierLevel = spec.tierLevel < 0 ? 0 : spec.tierLevel;
            properties.kind = kindOf(spec);
            // Hoe has no attack modifier and no mining speed override.
            properties.speed = properties.kind == ToolKind::Hoe || spec.tierSpeed < 0
                ? 1.0f : float(spec.tierSpeed);
            properties.attackDamage = spec.attackDamage < 0 ? 0.0f : float(spec.attackDamage);
            result.push_back(properties);
        }
        return result;
    }();
    for (const auto& properties : table)
        if (properties.itemId == itemId) return &properties;
    return nullptr;
}

float attackDamage(int itemId) {
    // Source Player ATTACK_DAMAGE base is 1; a held item adds its modifier.
    const auto* properties = toolProperties(itemId);
    if (!properties || properties->kind == ToolKind::Armor) return 1.0f;
    return 1.0f + properties->attackDamage;
}

float blockHardness(int tileId) {
    for (const auto& entry : kHardness)
        if (entry.tileId == tileId) return entry.hardness;
    // The table above is a partial audit and the generated catalog carries no
    // hardness, so an unlisted tile is unknown: survival refuses to break it
    // (README 1.0.14) rather than guessing a mining time.
    return -1.0f;
}

bool canHarvest(int itemId, int tileId, int) {
    const float hardness = blockHardness(tileId);
    if (hardness < 0) return false;
    const auto* properties = toolProperties(itemId);
    const ToolKind kind = properties ? properties->kind : ToolKind::None;
    if (kind == ToolKind::Sword) return tileId == 30; // Source: only web.
    int required = -1;
    for (const auto& entry : kHarvest)
        if (entry.tileId == tileId) { required = entry.level; break; }
    if (required < 0) return true; // Always-destroyable material in source.
    if (kind == ToolKind::Pickaxe) return properties->tierLevel >= required;
    // Source gates these ores through the pickaxe only; shovel/axe/hoe/hand cannot.
    return false;
}

float destroyProgress(int itemId, int tileId, int data, bool, bool inWater) {
    const float hardness = blockHardness(tileId);
    if (hardness < 0) return 0.0f;
    const auto* properties = toolProperties(itemId);
    float speed = 1.0f;
    if (properties) {
        switch (properties->kind) {
            case ToolKind::Sword: speed = swordSpeed(tileId); break;
            case ToolKind::Pickaxe: speed = listed(std::begin(kPickaxeTiles), std::end(kPickaxeTiles), tileId) ? properties->speed : 1.0f; break;
            case ToolKind::Shovel: speed = listed(std::begin(kShovelTiles), std::end(kShovelTiles), tileId) ? properties->speed : 1.0f; break;
            case ToolKind::Axe: speed = listed(std::begin(kAxeTiles), std::end(kAxeTiles), tileId) ? properties->speed : 1.0f; break;
            default: speed = 1.0f; break; // Hoe and armor never speed up mining.
        }
    }
    // Source Player::getDestroySpeed: no aqua affinity here, and the pinned
    // console source has the airborne penalty commented out.
    if (inWater) speed /= 5.0f;
    if (hardness == 0) return 1.0f; // Instant in source (division by zero).
    return speed / hardness / (canHarvest(itemId, tileId, data) ? 30.0f : 100.0f);
}

int miningWear(int itemId, int tileId) {
    const auto* properties = toolProperties(itemId);
    if (!properties || blockHardness(tileId) == 0) return 0;
    switch (properties->kind) {
        case ToolKind::Sword: return 2; // Source WeaponItem::mineBlock
        case ToolKind::Shovel:
        case ToolKind::Pickaxe:
        case ToolKind::Axe: return 1; // Source DiggerItem::mineBlock
        default: return 0; // Hoe/armor do not wear from mining.
    }
}

int attackWear(int itemId) {
    const auto* properties = toolProperties(itemId);
    if (!properties) return 0;
    switch (properties->kind) {
        case ToolKind::Sword: return 1; // Source WeaponItem::hurtEnemy
        case ToolKind::Shovel:
        case ToolKind::Pickaxe:
        case ToolKind::Axe: return 2; // Source DiggerItem::hurtEnemy
        default: return 0;
    }
}

bool knownDropRule(int tileId) {
    for (const auto& drop : kDrops)
        if (drop.tileId == tileId) return true;
    // The kDrops table is a partial audit. For any other catalog tile we fall
    // back to "drop the block itself" (the source Tile::getResource default),
    // so survival mining never deadlocks on an unlisted variant.
    for (int i = 0; i < legacy::kBlockCount; ++i)
        if (legacy::kBlockSpecs[i].tileId == tileId) return true;
    return false;
}

int itemForTile(int tileId, int data);

std::vector<BlockDrop> drops(int heldItemId, int tileId, int data) {
    // Shears-only plants: LeafTile/TallGrass/DeadBushTile/VineTile::playerDestroy
    // drop the block itself only when the selected item is shears. Without
    // shears this port drops nothing (the source's random sapling/apple/seed
    // loot is deliberately not reproduced).
    constexpr int kShearsId = 359; // Item::shears (Item.cpp:399, id 256 + 103)
    switch (tileId) {
    case 18: // leaves: LeafTile.cpp:271, data & LEAF_TYPE_MASK
        return heldItemId == kShearsId ? std::vector<BlockDrop>{{18, data & 3, 1}} : std::vector<BlockDrop>{};
    case 31: // tall grass / fern: TallGrass.cpp:82, keeps data
        return heldItemId == kShearsId ? std::vector<BlockDrop>{{31, data, 1}} : std::vector<BlockDrop>{};
    case 32: // dead bush: DeadBushTile.cpp:30
        return heldItemId == kShearsId ? std::vector<BlockDrop>{{32, 0, 1}} : std::vector<BlockDrop>{};
    case 106: // vine: VineTile.cpp:372, aux 0
        return heldItemId == kShearsId ? std::vector<BlockDrop>{{106, 0, 1}} : std::vector<BlockDrop>{};
    default: break;
    }
    for (const auto& drop : kDrops) {
        if (drop.tileId != tileId) continue;
        // First match wins. Known empty drops (count 0) and tool-gated ores
        // mined without a sufficient pickaxe yield nothing, as in source.
        if (!drop.count || (drop.needsTool && !canHarvest(heldItemId, tileId, data))) return {};
        int aux = drop.aux;
        if (aux < 0) {
            // -1 = keep the material variant (planks, wool, logs ...): use the
            // aux of the item that represents this exact block state.
            const int index = itemForTile(tileId, data);
            if (index < 0 || index >= sourceitems::kItemCount) return {};
            aux = sourceitems::kItems[index].aux;
        }
        return {{drop.itemId, aux, drop.count}};
    }
    // Unlisted catalog tile: source Tile::getResource default, the block itself.
    if (!canHarvest(heldItemId, tileId, data)) return {};
    const int index = itemForTile(tileId, data);
    if (index < 0 || index >= sourceitems::kItemCount) return {};
    const auto& item = sourceitems::kItems[index];
    return {{item.itemId, item.aux, 1}};
}

int itemForTile(int tileId, int data) {
    for (int i = 0; i < legacy::kBlockCount; ++i) {
        const auto& s = legacy::kBlockSpecs[i];
        if (s.tileId == tileId && s.data == data) {
            return sourceitems::itemForBlockState(i); // catalog index
        }
    }
    // Fall back to any state of this tile.
    for (int i = 0; i < legacy::kBlockCount; ++i)
        if (legacy::kBlockSpecs[i].tileId == tileId)
            return sourceitems::itemForBlockState(i);
    return -1;
}
} // namespace mcgame
