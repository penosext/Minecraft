#include "GameActions.hpp"

#include "ItemCatalog.hpp"
#include "LegacyCatalog.hpp"
#include "Scene.hpp"
#include "ToolRules.hpp"
#include "World.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

namespace mcgame {
namespace {
constexpr float TickSeconds = .05f;      // Source 20 TPS.
constexpr int BreakDelayTicks = 5;       // Source destroyDelay after a break.
constexpr double MaxCatchUpSeconds = .1; // Matches World/Scene dt clamping.

int tileOf(uint16_t block) {
    if (!block || block > legacy::kBlockCount) return -1;
    return legacy::kBlockSpecs[block - 1].tileId;
}
int dataOf(uint16_t block) {
    if (!block || block > legacy::kBlockCount) return 0;
    return legacy::kBlockSpecs[block - 1].data;
}
} // namespace

float GameActions::blockReach(mcinventory::GameMode mode) {
    // Source CreativeMode 5 and local SurvivalMode 4; multiplayer's 4.5 is a
    // different path and is deliberately not used here.
    return mode == mcinventory::GameMode::Creative ? 5.0f : 4.0f;
}
float GameActions::attackReach(mcinventory::GameMode mode) {
    return mode == mcinventory::GameMode::Creative ? 6.0f : 3.0f;
}

void GameActions::clearTarget() {
    hasTarget_ = false;
    targetBlock_ = 0;
    selectedSlot_ = selectedItem_ = selectedAux_ = selectedDamage_ = -1;
    mining_.progress = 0;
    mining_.active = false;
}

void GameActions::resetMining() {
    clearTarget();
    mining_ = MiningState {};
    remainder_ = 0;
    delayTicks_ = 0;
}

bool GameActions::finishMining(mcworld::World& world, mcinventory::Inventory& inventory,
                               const mcworld::BlockHit& hit, float yaw, float pitch) {
    const int tile = tileOf(hit.block);
    const int data = dataOf(hit.block);
    if (mode_ == mcinventory::GameMode::Creative) {
        return world.mine(yaw, pitch, blockReach(mode_));
    }
    if (!knownDropRule(tile)) {
        // No audited drop rule and no catalog fallback: still break the block
        // (the source lets you mine it, you just get nothing). Only refuse
        // the drop, not the break — otherwise survival deadlocks on variants.
        mining_.unsupportedLoot = true;
        return world.mine(yaw, pitch, blockReach(mode_));
    }
    const auto gathered = drops(inventory.selectedItemId(), tile, data);
    // Stage the inventory mutation first, but treat an overflow as "break the
    // block, drop nothing" rather than refusing the break. A full inventory
    // must not make an otherwise-breakable block un-minable.
    auto candidate = inventory;
    bool overflow = false;
    for (const auto& drop : gathered) {
        if (!candidate.addItem(drop.itemId, drop.aux, drop.count)) {
            overflow = true;
            break;
        }
    }
    const int wear = miningWear(inventory.selectedItemId(), tile);
    if (wear > 0) {
        // Tool wear failure means the tool would break: refuse the whole
        // break, as the source does. (damageSelected never returns false for
        // survival tools with wear <= remaining durability.)
        if (!candidate.damageSelected(wear)) return false;
    }
    if (!world.mine(yaw, pitch, blockReach(mode_))) return false;
    if (overflow) {
        // Block is gone; the dropped items are discarded. This mirrors the
        // source when the player's inventory is full: the block still breaks
        // and the drops are lost (they fall on the ground, which this port
        // does not model as an entity). Report it so the UI can warn.
        mining_.inventoryFull = true;
        return true;
    }
    inventory = std::move(candidate);
    return true;
}

const MiningState& GameActions::tickMining(mcworld::World& world, mcinventory::Inventory& inventory,
                                           float yaw, float pitch, bool held, float dt) {
    mining_.broken = false;
    mining_.inventoryFull = false;
    mining_.unsupportedLoot = false;
    if (inventory.opened() && inventory.gameMode() != mode_) {
        mode_ = inventory.gameMode();
        clearTarget();
        delayTicks_ = 0;
    }
    if (!held || !world.loaded() || !std::isfinite(yaw) || !std::isfinite(pitch) ||
        !std::isfinite(dt) || dt < 0) {
        if (!held) { clearTarget(); remainder_ = 0; }
        return mining_;
    }
    const auto* properties = inventory.opened() ? toolProperties(inventory.selectedItemId()) : nullptr;
    // Source MultiPlayerGameMode: a creative player holding a sword cannot mine.
    if (mode_ == mcinventory::GameMode::Creative && properties && properties->kind == ToolKind::Sword) {
        clearTarget();
        return mining_;
    }
    const auto hit = world.pick(yaw, pitch, blockReach(mode_));
    if (!hit.hit) { clearTarget(); return mining_; }
    // An unloaded column is "unknown", not air and not a new target: never
    // start mining (or show a crack) on it, and keep any real target's
    // progress so a ray briefly crossing a streaming edge does not cancel.
    if (hit.unknown) return mining_;
    const int slot = inventory.opened() ? inventory.selectedSlot() : -1;
    const int item = inventory.opened() ? inventory.selectedItemId() : -1;
    const int aux = inventory.opened() ? inventory.selectedAux() : -1;
    const int damage = inventory.opened() ? inventory.selectedDamage() : -1;
    // Durability is NOT part of target identity: mining wears the tool and
    // that wear must not reset progress mid-swing. Slot / item / aux / block
    // are the real identity of "what am I mining with and at".
    const bool sameTarget = hasTarget_ && mining_.x == hit.x && mining_.y == hit.y && mining_.z == hit.z &&
                        targetBlock_ == hit.block && selectedSlot_ == slot && selectedItem_ == item &&
                        selectedAux_ == aux;
    if (!sameTarget) {
        // A new target, block state or held item restarts progress, as upstream does.
        mining_.progress = 0;
        remainder_ = 0;
        delayTicks_ = 0;
    }
    hasTarget_ = true;
    mining_.active = true;
    mining_.x = hit.x; mining_.y = hit.y; mining_.z = hit.z;
    targetBlock_ = hit.block;
    selectedSlot_ = slot; selectedItem_ = item; selectedAux_ = aux; selectedDamage_ = damage;
    const int tile = tileOf(hit.block);
    const int data = dataOf(hit.block);
    const float hardness = blockHardness(tile);
    if (hardness < 0) { mining_.progress = 0; return mining_; } // Indestructible or unknown.
    const float perTick = mode_ == mcinventory::GameMode::Creative
        ? 1.0f : destroyProgress(item, tile, data, world.grounded(), world.underWater());
    remainder_ = std::min(remainder_ + double(dt), MaxCatchUpSeconds);
    while (remainder_ >= double(TickSeconds)) {
        remainder_ -= double(TickSeconds);
        if (delayTicks_ > 0) { --delayTicks_; continue; }
        mining_.progress += perTick;
        if (mining_.progress < 1.0f) continue;
        if (finishMining(world, inventory, hit, yaw, pitch)) {
            mining_.broken = true;
            mining_.progress = 0;
            delayTicks_ = BreakDelayTicks;
            clearTarget();
            mining_.active = false;
            break;
        }
        // A refused break keeps full progress without consuming the tool.
        mining_.progress = 1.0f;
        break;
    }
    mining_.progress = std::clamp(mining_.progress, 0.0f, 1.0f);
    return mining_;
}

std::string GameActions::miningInfoJson() const {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<float>::max_digits10)
        << "{\"active\":" << (mining_.active ? "true" : "false")
        << ",\"broken\":" << (mining_.broken ? "true" : "false")
        << ",\"progress\":" << mining_.progress
        << ",\"x\":" << mining_.x << ",\"y\":" << mining_.y << ",\"z\":" << mining_.z
        << ",\"inventoryFull\":" << (mining_.inventoryFull ? "true" : "false")
        << ",\"unsupportedLoot\":" << (mining_.unsupportedLoot ? "true" : "false") << '}';
    return out.str();
}

bool GameActions::use(mcworld::World& world, mcinventory::Inventory& inventory, float yaw, float pitch) {
    if (!world.loaded() || !std::isfinite(yaw) || !std::isfinite(pitch)) return false;
    mode_ = inventory.opened() ? inventory.gameMode() : mode_;
    const float reach = blockReach(mode_);
    // Source order: Tile::use first; a handled use consumes the click entirely.
    if (world.useBlock(yaw, pitch, reach)) return true;
    if (!inventory.opened() || inventory.emptyHand()) return false;
    const int item = inventory.selectedItemId();
    const auto* properties = toolProperties(item);
    if (properties && properties->kind == ToolKind::Hoe) {
        if (!world.till(yaw, pitch, reach)) return false;
        // Source HoeItem::useOn wears one point after a successful till.
        if (mode_ == mcinventory::GameMode::Survival) inventory.damageSelected(1);
        return true;
    }
    if (!world.placeItem(yaw, pitch, item, inventory.selectedAux(), reach)) return false;
    if (mode_ == mcinventory::GameMode::Survival) inventory.consumeSelected(1);
    return true;
}

std::string GameActions::attack(mcworld::World& world, mcscene::Scene& scene,
                                mcinventory::Inventory& inventory, float yaw, float pitch) {
    mode_ = inventory.opened() ? inventory.gameMode() : mode_;
    const int item = inventory.opened() ? inventory.selectedItemId() : -1;
    const auto result = scene.attack(world, yaw, pitch, attackReach(mode_),
                                     mode_ == mcinventory::GameMode::Creative,
                                     mcgame::attackDamage(item));
    // Source Player::attack wears the held item on any living hit, even when the
    // invulnerability window rejects the damage itself. Swinging at air does not.
    if (result.find("\"hit\":true") != std::string::npos && mode_ == mcinventory::GameMode::Survival) {
        const int wear = attackWear(item);
        if (wear > 0) inventory.damageSelected(wear);
    }
    return result;
}

} // namespace mcgame
