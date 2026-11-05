#pragma once

#include "PlayerSurvival.hpp"

namespace mcworld { class World; }
namespace mcscene { class Scene; }
namespace mcinventory { class Inventory; }

namespace mcplayer {
struct FrameResult {
    bool died = false;             // Health reached 0 this frame.
    bool ate = false;              // A food item was consumed this frame.
    bool inventoryChanged = false; // Armor wear/break, food consumed, death drops or pickup.
    int pickedUp = 0;              // Items taken from item entities this frame.
};
// Per-frame glue, called right after Scene::update. Order: armor value from
// worn items, zombie hits (Monster::doHurtTarget -> Player::hurt), Survival
// tick, armor wear (Inventory::hurtArmor), eat completion (consume first, then
// FoodData::eat), death drops, item pickup (Player::aiStep touch, alive only,
// creative too), then next frame's zombie targeting.
FrameResult survivalFrame(Survival& survival, mcworld::World& world, mcscene::Scene& scene,
                          mcinventory::Inventory& inventory, float dt);
// Right-button "use" for the held item while held is true: plain food starts or
// continues eating (FoodItem::use); an ArmorItem is equipped once per press
// (pressed = rising edge). held=false stops eating (Player::releaseUsingItem).
bool useHeldItem(Survival& survival, mcinventory::Inventory& inventory, bool held, bool pressed);
// Death transaction: Player::die -> Inventory::dropAll unless KeepInventory.
// Every main and armor stack becomes an item entity around the player
// (Player::drop(item, true)). Crash-safe order, each step its own atomic file:
//   1. (fresh death) clear a stale scene "drops applied" flag, then save
//      survival with deathDropsPending = true;
//   2. spawn the items and set the scene's applied flag, save the scene;
//   3. clear and save the inventory; 4. save survival with pending = false.
// A crash before 2 re-drops from the still-full saved inventory; after 2 the
// applied flag stops a second drop and only the inventory is cleared. persist
// = false is the re-application of a pending death found on open.
void applyDeathDrops(Survival& survival, mcworld::World& world, mcscene::Scene& scene,
                     mcinventory::Inventory& inventory, bool keepInventory, bool persist);
} // namespace mcplayer
