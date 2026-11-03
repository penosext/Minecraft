#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mcworld { class World; }

namespace mcplayer {
// Independent player statistics. The caller owns movement, inventory consumption,
// respawn location and autosave scheduling. open() is read-only and transactional.
class Survival {
public:
    void open(const mcworld::World& world);
    void save(const mcworld::World& world);
    // 20 TPS; each call accepts at most .1 seconds, with no catch-up backlog.
    void update(mcworld::World& world, float dt, bool creative, bool naturalRegeneration);
    bool hurt(float damage, bool creative = false);
    bool eat(int nutrition, float saturationModifier, bool creative = false);
    void exhaust(float amount);
    bool dead() const { return health_ <= 0; }
    float health() const { return health_; }
    int food() const { return food_; }
    float saturation() const { return saturation_; }
    int air() const { return air_; }
    // Resets statistics/timers only; preserves world binding and difficulty.
    void respawn();
    std::string infoJson() const;
    bool setDifficulty(int difficulty); // 0 peaceful, 1 easy, 2 normal, 3 hard
    int difficulty() const { return difficulty_; }
    float spawnX() const { return spawnX_; }
    float spawnY() const { return spawnY_; }
    float spawnZ() const { return spawnZ_; }

private:
    void requireWorld(const mcworld::World& world) const;
    void tick(mcworld::World& world, bool creative, bool naturalRegeneration);
    float health_ = 20, saturation_ = 5, exhaustion_ = 0;
    int food_ = 20, air_ = 300, difficulty_ = 2;
    int foodTicks_ = 0, hurtTicks_ = 0, fireTicks_ = 0, peacefulTicks_ = 0;
    float lastDamage_ = 0, fallDistance_ = 0, lastY_ = 0;
    double tickRemainder_ = 0;
    bool positionKnown_ = false, creative_ = false;
    float spawnX_ = 0, spawnY_ = 0, spawnZ_ = 0;
    bool opened_ = false, original_ = false;
    std::string path_, root_;
    uint64_t rootDevice_ = 0, rootInode_ = 0, worldDevice_ = 0, worldInode_ = 0;
    uint32_t seed_ = 0, sourceHash_ = 0;
    int width_ = 0, height_ = 0, depth_ = 0;
    std::vector<unsigned char> source_, persisted_;
};
} // namespace mcplayer
