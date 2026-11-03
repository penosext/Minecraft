#pragma once

#include "World.hpp"
#include <functional>
#include <vector>

namespace mcworld {
// Source-audited Tile::use subset, NOT a Tile engine. There are no ticks, no
// addToTickNextTick, no redstone propagation, no neighbour updates and no tile
// entities here, so any source behaviour that depends on them is reported as
// unsupported instead of being approximated. See tools/extract-interaction.py
// for the per-function audit (file, line range, SHA-256) behind every rule.
struct InteractionChange { int x, y, z, renderIndex; };

// Mirrors the three distinguishable outcomes of the source dispatch in
// MultiPlayerGameMode::useItemOn (Minecraft.Client/MultiPlayerGameMode.cpp:283).
// Source `Tile::use` returns bool; `didSomething == true` suppresses the later
// item/placement branch at :330-357. Consumed vs Changed is our own split so a
// caller can tell "handled, nothing to publish" from "handled, publish this".
enum class InteractionResult {
    Unhandled, // Source use() returned false, or the rule is unsupported here.
    Consumed,  // Source use() returned true with no world edit (iron door).
    Changed,   // Source use() returned true and produced metadata changes.
};

struct InteractionContext {
    BlockHit hit;
    // MultiPlayerGameMode.cpp:297 gates use() behind
    // `!player->isSneaking() || player->getCarriedItem() == nullptr`, so a
    // sneaking player holding an item forces placement. World::useBlock has no
    // inventory or sneak state and therefore always passes false/false; the
    // gate exists for the caller that does own that state.
    bool sneaking = false, holdingItem = false;
    std::function<uint16_t(int, int, int)> blockAt;   // catalog index + 1
    std::function<bool(int, int, int)> available;     // in world AND chunk loaded
};

// Exact catalog metadata only, never a representative-orientation fallback.
// Returns the render index for tileId+data, or -1 when that state is absent
// from the embedded catalog. Thin wrapper over placementState so interaction
// and placement can never disagree about which states exist.
int interactionState(int tileId, int data);

// True only for tiles this subset actually acts on. Deliberately false for
// every tile whose source use() needs a tile entity, a GUI or a tick, and for
// tiles whose toggled state is missing from the catalog (lever 69, button 77,
// repeater 93 ship data=0 only). Reported honestly rather than faked.
bool interactionSupported(int tileId);

// Plans the interaction for context.hit. On anything but Changed the output is
// cleared. Never reads clickedFace/clickX/clickY/clickZ: every source use() in
// this subset ignores them (verified for DoorTile, LeverTile, ButtonTile and
// RepeaterTile), so block coordinates are sufficient. World validates the saved
// palette and publishes the plan atomically.
InteractionResult planBlockInteraction(const InteractionContext& context,
                                       std::vector<InteractionChange>& changes);
} // namespace mcworld
