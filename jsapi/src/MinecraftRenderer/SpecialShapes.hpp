#pragma once

namespace legacy {
class TileRenderer;
class Tile;
struct BlockSpec;

// Isolated, full-bright preview geometry, not the complete world/state renderer.
// Appends quads to Tesselator and restores the caller's TileRenderer state.
// Returns false, without emitting geometry, for an unrecognized shape.
bool renderSpecialShape(TileRenderer& renderer, Tile* tile, const BlockSpec& spec,
                        float r, float g, float b);
} // namespace legacy
