#pragma once

namespace mcscene {
// Shared scene/renderer contract; the scene owns spawning, movement, AI and saves.
enum class MobKind { Pig, Cow, Sheep, Chicken, Zombie };
struct Mob {
    int id;
    MobKind kind;
    // Absolute world block coordinates; y is the bottom of the feet.
    float x, y, z;
    // Degrees, 0 faces +Z, 90 faces +X, matching the world camera.
    float yaw;
    // Radians, periodic at 2*pi; scene keeps this constant while idle.
    float walkPhase;
    // Appended defaults preserve existing renderer aggregate initializers. Only
    // Scene-owned mobs have initialized health; zero means unspecified there.
    float health = 0;
    float maxHealth = 0;
    // Original hurt flash duration in 20 Hz ticks (10 on a successful hit).
    float hurtTime = 0;
};
} // namespace mcscene

namespace mcscene {
// Dropped item (ItemEntity.cpp). Shared contract between the item store, the
// renderer and the save file. Absolute world coordinates; y is the BOTTOM of
// the .25 x .25 x .25 box (Entity bb->y0); the source entity's y is
// y0 + heightOffset (.125, ItemEntity.cpp:30), which the renderer adds back.
struct ItemEntity {
    int itemId = 0, aux = 0, count = 0, damage = 0; // ItemInstance id/aux/count/damage
    double x = 0, y = 0, z = 0;
    float xd = 0, yd = 0, zd = 0;  // Velocity per 20 Hz tick.
    int age = 0;                   // ItemEntity::age; removed at LIFETIME (6000).
    int pickupDelay = 0;           // ItemEntity::throwTime (10 block drop, 40 player drop).
    int health = 5;                // ItemEntity::health (lava/fire damage).
    float bobOffs = 0;             // Random phase for bob/spin (ItemEntity.cpp:23).
    bool onGround = false;
    int stackLimit = 64;           // Transient cache: 1 for damageable items.
};
} // namespace mcscene
