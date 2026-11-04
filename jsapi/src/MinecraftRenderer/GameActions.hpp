#pragma once

#include "Inventory.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace mcworld { class World; struct BlockHit; }
namespace mcscene { class Scene; }

namespace mcgame {

struct MiningState {
    bool active = false;
    bool broken = false; // Successful break during the most recent tickMining call.
    bool inventoryFull = false;
    bool unsupportedLoot = false; // Survival pauses rather than inventing unknown drops.
    bool protectedBlock = false; // TileHooks::canBreak refused (e.g. non-empty chest).
    float progress = 0; // Normalized [0,1], for UI; no crack-animation requirement.
    int x = 0, y = 0, z = 0;
};

// Optional block-entity hooks (containers). Empty hooks keep the old behaviour.
struct TileHooks {
    std::function<bool(int x, int y, int z)> canBreak; // false => refuse the break.
    std::function<void(int x, int y, int z)> removed;  // after a successful break.
    std::function<bool(int x, int y, int z)> use;      // Tile::use; true consumes the click.
};

// Main-thread gameplay policy over World/Inventory/Scene; no saving, UI or SDK.
// Call resetMining on world/mode changes and when gameplay input is suspended.
class GameActions {
public:
    const MiningState& tickMining(mcworld::World& world, mcinventory::Inventory& inventory,
                                  float yaw, float pitch, bool held, float dt);
    void resetMining();
    std::string miningInfoJson() const;
    bool use(mcworld::World& world, mcinventory::Inventory& inventory, float yaw, float pitch);
    std::string attack(mcworld::World& world, mcscene::Scene& scene,
                       mcinventory::Inventory& inventory, float yaw, float pitch);

    // Local SurvivalMode range, not multiplayer's 4.5-block range.
    void setTileHooks(TileHooks hooks) { hooks_ = std::move(hooks); }

    static float blockReach(mcinventory::GameMode mode);
    static float attackReach(mcinventory::GameMode mode);

private:
    bool finishMining(mcworld::World& world, mcinventory::Inventory& inventory,
                      const mcworld::BlockHit& hit, float yaw, float pitch);
    void clearTarget();

    TileHooks hooks_;
    MiningState mining_;
    double remainder_ = 0;
    int delayTicks_ = 0;
    bool hasTarget_ = false;
    uint16_t targetBlock_ = 0;
    int selectedSlot_ = -1, selectedItem_ = -1, selectedAux_ = -1, selectedDamage_ = -1;
    mcinventory::GameMode mode_ = mcinventory::GameMode::Creative;
};

} // namespace mcgame
