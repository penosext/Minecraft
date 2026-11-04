#include "SurvivalFrame.hpp"
#include "Inventory.hpp"
#include "ItemCatalog.hpp"
#include "Scene.hpp"
#include "World.hpp"

#include <exception>

namespace mcplayer {
FrameResult survivalFrame(Survival& survival, mcworld::World& world, mcscene::Scene& scene,
                          mcinventory::Inventory& inventory, float dt) {
    FrameResult result;
    const bool creative = inventory.gameMode() == mcinventory::GameMode::Creative;
    // A death saved before its drops were applied (crash/kill between saves).
    if (survival.deathDropsPending()) {
        applyDeathDrops(survival, world, inventory, scene.keepInventoryEnabled(), false);
        result.inventoryChanged = true;
    }
    survival.setArmorValue(inventory.armorValue());
    // Player::updateFrameTick: a changed selection stops using the item.
    if (survival.eatingItem() >= 0 &&
        (inventory.selectedSlot() != survival.eatingSlot() || inventory.selectedItemId() != survival.eatingItem()))
        survival.stopEating();
    // Zombie hits recorded by the scene tick(s) of this frame.
    for (const auto& hit : scene.takePlayerHits()) survival.hurt(DamageSource::MobAttack, hit.damage);
    survival.update(world, dt, creative, scene.rule(mcworld::WorldRule::NaturalRegeneration));
    for (float wear : survival.takeArmorWear()) {
        const int before = inventory.armorValue();
        inventory.hurtArmor(wear);
        result.inventoryChanged = true;
        if (inventory.armorValue() != before) survival.setArmorValue(inventory.armorValue());
    }
    if (survival.takeEatCompleted()) {
        // FoodItem::useTimeDepleted: count-- before eat; identity re-checked.
        if (inventory.selectedSlot() == survival.eatingSlot() && inventory.selectedItemId() == survival.eatingItem() &&
            inventory.consumeSelected(1)) {
            survival.finishEating();
            result.ate = result.inventoryChanged = true;
        } else survival.stopEating();
    }
    if (survival.takeDied()) {
        result.died = true;
        survival.setDeathDropsPending(true);
        applyDeathDrops(survival, world, inventory, scene.keepInventoryEnabled(), true);
        result.inventoryChanged = true;
    }
    // Monster targeting: survival, alive, not peaceful (Player.cpp:1400-1402
    // zeroes mob damage on peaceful anyway).
    scene.setPlayerTargetable(!creative && !survival.dead() && survival.difficulty() > 0);
    return result;
}

bool useHeldItem(Survival& survival, mcinventory::Inventory& inventory, bool held, bool pressed) {
    if (!held) { survival.stopEating(); return false; }
    if (inventory.gameMode() != mcinventory::GameMode::Survival || survival.dead() || inventory.emptyHand()) return false;
    const int item = inventory.selectedItemId();
    if (const auto* food = sourceitems::findFood(item))
        return survival.startEating(item, inventory.selectedSlot(), food->nutrition, food->saturationModifier, food->eatTicks);
    // Equip only on the press edge so a held button does not swap back and forth.
    return pressed && inventory.equipSelectedArmor();
}

void applyDeathDrops(Survival& survival, mcworld::World& world, mcinventory::Inventory& inventory,
                     bool keepInventory, bool persist) {
    // Persist the pending flag first; a failed save is retried by autosave and
    // never blocks the in-memory transaction.
    if (persist) { try { survival.save(world); } catch (const std::exception&) {} }
    if (!keepInventory) inventory.clearMainAndArmor();
    if (persist) {
        try { inventory.save(world); survival.setDeathDropsPending(false); survival.save(world); }
        catch (const std::exception&) { survival.setDeathDropsPending(false); }
    } else survival.setDeathDropsPending(false);
}
} // namespace mcplayer
