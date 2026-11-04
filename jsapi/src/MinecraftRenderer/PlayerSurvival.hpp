#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mcworld { class World; }

namespace mcplayer {
// DamageSource.cpp:10-22 subset. bypassArmor sources skip armor and add no food
// exhaustion; MobAttack (EntityDamageSource) scales with difficulty.
enum class DamageSource { Generic, Fall, Drown, Starve, InFire, OnFire, Lava, MobAttack };

// Independent player statistics. The caller owns movement, inventory consumption,
// respawn location and autosave scheduling. open() is read-only and transactional.
class Survival {
public:
    void open(const mcworld::World& world);
    // Always writes the v2 layout (v1 + flags); v1 files open unchanged.
    void save(const mcworld::World& world);
    // 20 TPS; each call accepts at most .1 seconds, with no catch-up backlog.
    void update(mcworld::World& world, float dt, bool creative, bool naturalRegeneration);
    // Player::hurt -> LivingEntity::hurt -> Player::actuallyHurt. Returns true
    // when the hit was accepted (health may still be unchanged after armor).
    bool hurt(DamageSource source, float damage);
    // Compatibility: generic (armor-bypassing, unscaled) damage.
    bool hurt(float damage, bool creative = false);
    bool eat(int nutrition, float saturationModifier, bool creative = false);
    void exhaust(float amount);
    bool dead() const { return health_ <= 0; }
    float health() const { return health_; }
    float lastHealth() const { return lastHealth_; }
    int food() const { return food_; }
    float saturation() const { return saturation_; }
    float exhaustion() const { return exhaustion_; }
    int air() const { return air_; }
    int hurtTicks() const { return hurtTicks_; }
    int fireTicks() const { return fireTicks_; }
    // Transient tick counter for HUD jitter (Gui.cpp tickCount); not persisted.
    uint32_t tickCount() const { return tickCount_; }

    // Armor (Player::getArmorValue), set by the caller from worn items each frame.
    void setArmorValue(int value);
    int armorValue() const { return armor_; }
    // Pre-armor damage of every armor-reduced hit since the last call, for
    // Inventory::hurtArmor (LivingEntity::getDamageAfterArmorAbsorb). Transient.
    std::vector<float> takeArmorWear() { std::vector<float> out; out.swap(armorWear_); return out; }

    // FoodItem::use -> Player::startUsingItem(EAT_DURATION); updateFrameTick
    // decrements once per tick. Requires survival, alive and FoodData::needsFood.
    bool startEating(int itemId, int slot, int nutrition, float saturationModifier, int ticks);
    void stopEating() { eatTicks_ = 0; eatCompleted_ = false; eatItem_ = eatSlot_ = -1; }
    bool eating() const { return eatTicks_ > 0; }
    // Item being eaten; stays valid after completion until finish/stopEating.
    int eatingItem() const { return eatItem_; }
    int eatingSlot() const { return eatSlot_; }
    // Completion edge: the caller consumes one item and only then calls
    // finishEating(), so a failed consume never feeds the player.
    bool takeEatCompleted() { const bool done = eatCompleted_; eatCompleted_ = false; return done; }
    bool finishEating();
    // LocalPlayer.cpp:227-230 input xa/ya *= 0.2 while using an item.
    float movementScale() const { return eating() ? .2f : 1.0f; }

    // Death edge (health reached 0). Transient.
    bool takeDied() { const bool d = died_; died_ = false; return d; }
    // Persisted v2 flag: the inventory still has to be cleared for a death that
    // was saved. Set before the inventory is touched so a crash cannot lose items
    // without a recorded death.
    bool deathDropsPending() const { return (flags_ & DeathDropsPending) != 0; }
    void setDeathDropsPending(bool value) { flags_ = value ? flags_ | DeathDropsPending : flags_ & ~DeathDropsPending; }

    // Resets statistics/timers only; preserves world binding, difficulty and flags.
    void respawn();
    std::string infoJson() const;
    bool setDifficulty(int difficulty); // 0 peaceful, 1 easy, 2 normal, 3 hard
    int difficulty() const { return difficulty_; }
    float spawnX() const { return spawnX_; }
    float spawnY() const { return spawnY_; }
    float spawnZ() const { return spawnZ_; }
    // 0 = no file yet, 1 = legacy, 2 = current.
    uint32_t persistedVersion() const { return persistedVersion_; }

    static constexpr uint32_t DeathDropsPending = 1;

private:
    void requireWorld(const mcworld::World& world) const;
    void tick(mcworld::World& world, bool creative, bool naturalRegeneration);
    void actuallyHurt(DamageSource source, float damage);
    float health_ = 20, saturation_ = 5, exhaustion_ = 0;
    int food_ = 20, air_ = 300, difficulty_ = 2;
    int foodTicks_ = 0, hurtTicks_ = 0, fireTicks_ = 0, peacefulTicks_ = 0;
    float lastDamage_ = 0, fallDistance_ = 0, lastY_ = 0;
    double tickRemainder_ = 0;
    bool positionKnown_ = false, creative_ = false;
    float spawnX_ = 0, spawnY_ = 0, spawnZ_ = 0;
    uint32_t flags_ = 0, persistedVersion_ = 0;
    // Transient (not saved): HUD/armor/eat/movement bookkeeping.
    float lastHealth_ = 20;
    int armor_ = 0;
    std::vector<float> armorWear_;
    int eatTicks_ = 0, eatItem_ = -1, eatSlot_ = -1, eatNutrition_ = 0;
    float eatSaturation_ = 0;
    bool eatCompleted_ = false, died_ = false;
    bool wasGrounded_ = false, tickPositionKnown_ = false;
    float tickX_ = 0, tickY_ = 0, tickZ_ = 0;
    uint32_t tickCount_ = 0;
    bool opened_ = false, original_ = false;
    std::string path_, root_;
    uint64_t rootDevice_ = 0, rootInode_ = 0, worldDevice_ = 0, worldInode_ = 0;
    uint32_t seed_ = 0, sourceHash_ = 0;
    int width_ = 0, height_ = 0, depth_ = 0;
    std::vector<unsigned char> source_, persisted_;
};
} // namespace mcplayer
