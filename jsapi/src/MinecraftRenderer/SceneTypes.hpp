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
