#pragma once

#include "SceneTypes.hpp"

#include <cstdint>
#include <vector>

namespace mcworld { class World; }

// Dropped items in the world view (Minecraft.Client ItemRenderer.cpp:60-197).
// Composite after renderWorldFrame (and mobs), before the hand/UI. Same camera
// as WorldRenderer/MobRenderer: eye = feet + 1.62, yaw/pitch degrees.
//  - bob = sin((age + a) / 10 + bobOffs) * .1 + .1, spin = ((age + a) / 20 +
//    bobOffs) rad around Y (ItemRenderer.cpp:71-72).
//  - Blocks whose held path is Block3D (TileRenderer::canRender) draw the held
//    renderTile mesh at scale 1/4 with 1..5 copies for count > 1/5/20/40 and
//    Random(187) offsets (ItemRenderer.cpp:74-118).
//  - Everything else is its 16x16 icon (items.png or the tile's terrain icon)
//    on a spinning plane, 1..4 copies for count < 2/16/32 (fancy
//    renderItemBillboard, ItemRenderer.cpp:228-262). Platform simplification:
//    one flat double-sided plane per copy instead of renderItem3D's extruded
//    1/16 slab, so a sprite seen exactly edge-on vanishes.
//  - Light: the world lighting cache at the item's cell (max of its cell and
//    the one above), with the WorldRenderer brightness ramp; face shade 1/.8/.6.
//  - Culled beyond Entity::shouldRenderAtSqrDistance (bb size .25 * 64 = 16).
//  - Terrain depth test: per-tile voxel DDA like MobRenderer (opaque cubes
//    occlude), plus a z-buffer between items. Mobs and items do not depth-test
//    each other.
void renderItemOverlay(uint32_t* pixels, int w, int h, const mcworld::World& world,
                       float yaw, float pitch, const std::vector<mcscene::ItemEntity>& items,
                       float daylight, float partialTick = 0);
