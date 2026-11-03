#pragma once

#include "SceneTypes.hpp"
#include "WorldRules.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace mcworld { class World; }

namespace mcscene {
// Original day/sky functions and original Random, with deliberately lightweight
// platform wander/collision/gravity AI, NOT the original Mob/Goal/pathfinder.
// Main-thread only. Coordinates are absolute blocks and Mob.y is feet height.
class Scene {
public:
    // Missing scene.bin is a compatible old world: starts at original tick 0
    // (dawn), populates nearby dry grass, and writes nothing until save(). Invalid
    // existing scene state throws without modifying the scene or the World.
    // v1/v2 migrate only in memory (v2 keeps HP and assumes last raw hit = 1).
    void open(const mcworld::World& world);
    // Independently atomic v3 scene.bin; only explicit save() upgrades versions.
    // Never writes World files. Rejects unknown, changed, linked/nonregular files
    // and replaced save-root/world directories.
    void save(const mcworld::World& world);
    // Real elapsed time at 20 TPS, 24000 ticks/day. dt capped at .1 seconds;
    // nonfinite/negative values throw. Fixed ticks make AI frame-rate invariant.
    void update(mcworld::World& world, float dt);
    // Eye = player feet + 1.62, yaw 0 = +Z, 90 = +X, pitch positive up.
    // Nearest visible ray/AABB target: normal cap 3, creative far-pick cap 6;
    // the caller explicitly selects farPickRange. Solid World cells
    // occlude (liquids do not). Invalid/nonfinite inputs throw without mutation.
    // Flat JSON: hit/damaged/killed/damage/cooldownTicks/killCount, plus target
    // id/kind/health/maxHealth/distance if hit. baseDamage is the caller's TOTAL
    // raw Player damage (fist 1, diamond sword 8), not an added weapon bonus.
    // Must be finite and in (0, 1024], a platform safety bound, before mutation.
    // The 20-tick timer at >10 rejects damage <= lastHurt; stronger hits apply
    // only the raw difference without resetting either timer. Armor follows
    // that comparison (default Zombie: raw * 23 / 25). No player-health armor,
    // equipment wear, crits, effects, retaliation or drops here.
    // Platform simplification: death immediately removes mob AND brain, not the
    // upstream 20-tick death animation. save() persists it like other scene state.
    std::string attack(const mcworld::World& world, float yaw, float pitch, float reach = 3.0f,
                       bool farPickRange = false, float baseDamage = 1.0f);
    // Same ray at normal 3-block reach. {} if no visible target, otherwise
    // id/kind/health/maxHealth/distance. Read-only; no timer or AI changes.
    std::string targetInfo(const mcworld::World& world, float yaw, float pitch,
                           bool farPickRange = false) const;
    const std::vector<Mob>& mobs() const { return mobs_; }
    uint64_t ticks() const { return ticks_; }
    // Celestial tick count driving sky/light. Equals ticks() unless
    // DoDaylightCycle was ever disabled, which holds this value still while
    // ticks() keeps running the simulation.
    uint64_t dayTicks() const { return ticks_ - frozenTicks_; }
    // Absolute celestial tick setter for the /time command. Adjusts the frozen
    // offset so dayTicks() lands exactly on the request while the simulation
    // clock keeps running: time and light stay derived from one value and
    // cannot drift apart. Rejects values that would overflow; mob timers and
    // AI are untouched. Persisted like any other scene state on save().
    bool setDayTicks(uint64_t ticks);
    // Original eased celestial fraction, not raw ticks / 24000.
    float timeOfDay() const;
    // Original clear-weather Level::getSkyDarken output, [.2, 1].
    float daylight() const;
    std::string summaryJson() const;

    // --- World rules -------------------------------------------------------
    // Scene owns rules persistence because the rules change scene behaviour.
    // They live in their own rules.json, NEVER in the World's level.json,
    // whose field count is exact-match validated and would reject old saves.
    const mcworld::WorldRules& rules() const { return rules_; }
    // Applies immediately. DoDaylightCycle=false holds celestial time at its
    // current value (lighting stays consistent with the frozen time, and the
    // freeze survives save/open); DoMobSpawning=false stops all population.
    // Other rules are stored and reported per WorldRules::enforcement.
    bool setRule(mcworld::WorldRule rule, bool value);
    bool rule(mcworld::WorldRule rule) const { return rules_.get(rule); }
    // Queries the gameplay layer calls instead of reading rules directly, so
    // GameActions never has to know where the rules are stored.
    bool tileDropsEnabled() const { return rules_.get(mcworld::WorldRule::DoTileDrops); }
    bool keepInventoryEnabled() const { return rules_.get(mcworld::WorldRule::KeepInventory); }
    // Replaces all rules from a JSON document; false leaves them unchanged.
    bool applyRulesJson(const std::string& text);
    std::string rulesJson() const;

private:
    struct Brain {
        float velocityY = 0;
        uint32_t turnTicks = 0;
        bool walking = false;
        uint32_t invulnerableTime = 0;
        float lastHurt = 0; // Last accepted pre-armor raw damage; persisted in v3.
    };
    void requireWorld(const mcworld::World& world) const;
    void tick(const mcworld::World& world);
    void populate(const mcworld::World& world, bool initial);
    // rules.json is independent of scene.bin: a missing file is a valid old
    // save that loads defaults and writes nothing until save().
    void openRules(int worldDirectory);
    void saveRules(int worldDirectory);

    bool opened_ = false;
    std::string root_, path_, name_;
    uint64_t rootDevice_ = 0, rootInode_ = 0, worldDevice_ = 0, worldInode_ = 0;
    uint32_t seed_ = 0;
    int width_ = 0, height_ = 0, depth_ = 0, nextId_ = 1;
    uint64_t ticks_ = 0, killCount_ = 0;
    // Celestial ticks withheld while DoDaylightCycle was false. Subtracting it
    // from ticks_ freezes the sky WITHOUT rewinding or branching the original
    // time functions, so lighting stays exactly consistent with what is saved.
    uint64_t frozenTicks_ = 0;
    // Pause population replacement for 15 seconds after a kill, even on reload.
    uint32_t populationCooldownTicks_ = 0;
    double remainder_ = 0;
    std::vector<Mob> mobs_;
    std::vector<Brain> brains_;
    // Exact last accepted disk bytes: no silent overwrite of unknown state.
    std::vector<unsigned char> persisted_;
    mcworld::WorldRules rules_;
    // Last accepted rules.json bytes, empty for an old save with no file.
    std::string persistedRules_;
};
} // namespace mcscene
