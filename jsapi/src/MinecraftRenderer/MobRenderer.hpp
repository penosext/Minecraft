#pragma once

#include "SceneTypes.hpp"
#include <cstdint>
#include <vector>

namespace mcworld { class World; }

// Composite after renderWorldFrame and before actor/UI. Straight ARGB32, w*h
// pixels, no padding. World-space cuboids, perspective-correct skin UVs and a
// shared entity z-buffer. Camera matches WorldRenderer: horizontal FOV=90 deg,
// eye=player feet+1.62, yaw/pitch degrees (positive pitch up, clamped +/-80).
// Mobs farther than 32 blocks from the eye, invalid kinds/nonfinite transforms,
// unloaded world, invalid dimensions (outside 1..16384) and null pixels are skipped.
// daylight [0,1] controls ambient/diffuse light (nonfinite becomes 1).
// World occlusion uses blockAt voxel DDA, lazily sampled at screen tiles
// (4px at 960 wide, 8px at 1920). Opaque catalog blocks occlude, transparent/
// cutout blocks do not. Solid walls/terrain hide mobs, with tile-size edge error;
// this is not the original game's renderer nor an exact translucent depth pass.
// Finite hurtTime>0 adds a red .3 skin tint without changing geometry/depth.
// Finite health/maxHealth>0 enables a text-free overhead bar within 16 blocks
// of the eye: perspective width clamped to 24..56px, 6px tall, straight-ARGB
// translucent black background and green (>1/2), yellow (>1/4), red fill.
// Only mobs with final visible body pixels qualify. Exact center/corner terrain
// rays gate the WHOLE bar (no tiled HUD occlusion); offscreen bars are skipped,
// reticle-overlapping bars lifted above it. Bars do not alter the body z-buffer. Zero-default
// HP keeps legacy images unchanged. Scene removes dead mobs from the input list;
// this renderer never caches entities and does not own health/death simulation.
// Near-plane polygons are clipped; no billboard sprites. Thread-local scratch
// storage reused each call. Scene owns AI, movement, spawning and persistence.
// Pig/sheep/zombie are classic-cuboid platform renderer approximations; cow and
// chicken use available original model cuboids. All PNGs are original assets.
void renderMobOverlay(uint32_t* pixels, int w, int h, const mcworld::World& world,
                      float yaw, float pitch, const std::vector<mcscene::Mob>& mobs,
                      float daylight);
