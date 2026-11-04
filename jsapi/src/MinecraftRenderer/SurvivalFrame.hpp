#pragma once

#include "PlayerSurvival.hpp"

namespace mcworld { class World; }
namespace mcscene { class Scene; }
namespace mcinventory { class Inventory; }

namespace mcplayer {
struct FrameResult {
    bool died = false;             // Health reached 0 this frame.
    bool ate = false;              // A food item was consumed this frame.
    bool inventoryChanged = false; // Armor wear/break, food consumed or death drops.
};
// Per-frame glue, called right after Scene::update. Order: armor value from
// worn items, zombie hits (Monster::doHurtTarget -> Player::hurt), Survival
// tick, armor wear (Inventory::hurtArmor), eat completion (consume first, then
// FoodData::eat), death drops, then next frame's zombie targeting.
FrameResult survivalFrame(Survival& survival, mcworld::World& world, mcscene::Scene& scene,
                          mcinventory::Inventory& inventory, float dt);
// Right-button "use" for the held item while held is true: plain food starts or
// continues eating (FoodItem::use); an ArmorItem is equipped once per press
// (pressed = rising edge). held=false stops eating (Player::releaseUsingItem).
bool useHeldItem(Survival& survival, mcinventory::Inventory& inventory, bool held, bool pressed);
// Death transaction: Player::die -> Inventory::dropAll unless KeepInventory.
// No item entities exist in this port, so dropped items are removed. Order is
// crash-safe: the pending flag is saved before the inventory is cleared.
void applyDeathDrops(Survival& survival, mcworld::World& world, mcinventory::Inventory& inventory,
                     bool keepInventory, bool persist);
} // namespace mcplayer
