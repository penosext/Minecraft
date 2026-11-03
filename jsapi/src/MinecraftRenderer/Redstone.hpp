#pragma once

#include <cstdint>
#include <functional>
#include <vector>

// Redstone signal queries and propagation.
//
// independent implementation, rules verified against observed behaviour.
//
// This module is PURE: it never holds a world, never mutates one, and never
// schedules anything. Callers supply a block-read callback and atomically
// commit the returned changes. Values exchanged with the caller are CURRENT
// catalog ids (legacy::kBlockSpecs index + 1, 0 == air), the same convention
// used by World/OriginalWorld/BlockCollision.
//
// Implemented: redstone dust charge decay and branching, repeater delay and
// one-way conduction, lever and button charge sources, redstone torch
// inversion.
//
// Deliberately NOT implemented, and not faked:
//   * Pistons, dispensers and hoppers. These need block entities and a moving
//     block/inventory model that this module has no access to.
//   * Button auto-release and repeater delay EXPIRY. Both are tick-driven.
//     buttonReleaseDelayTicks() and repeaterDelayTicks() report the correct
//     durations, but nothing here advances time: a pressed button stays
//     pressed until the caller schedules the release itself.
//   * Comparators, redstone lamps, rails, doors, trapdoors and tripwire.
//   * Torch burnout, repeater locking by a side-on repeater, and the
//     quasi-connectivity/update-order quirks of the original scheduler.
namespace mcredstone {

// Charge levels. Dust carries 0-15; a source emits the maximum.
constexpr int SignalNone = 0;
constexpr int SignalMin = 0;
constexpr int SignalMax = 15;

// Source tile ids, matching the upstream numbering the catalog is keyed on.
constexpr int TileRedstoneDust = 55;
constexpr int TileLever = 69;
constexpr int TileRedstoneTorchOff = 75;
constexpr int TileRedstoneTorchOn = 76;
constexpr int TileButtonStone = 77;
constexpr int TileRepeaterOff = 93;
constexpr int TileRepeaterOn = 94;
constexpr int TileButtonWood = 143;

// A located charge emitter. Reported by powerSources() for inspection and
// debugging; propagate() does not require the caller to build these.
struct PowerSource {
    int x = 0, y = 0, z = 0, level = 0;
};

// A single block change the caller should commit. renderIndex is a CURRENT
// catalog id (index + 1), directly storable via World/OriginalWorld setBlock.
struct Change {
    int x = 0, y = 0, z = 0, renderIndex = 0;
};

// Decoded tile/metadata behind a catalog id. tileId is -1 when the id is not a
// known render state, and 0 for air.
struct BlockState {
    int tileId = 0, data = 0;
};

using BlockReader = std::function<uint16_t(int, int, int)>;

// --- state decoding -------------------------------------------------------
// Catalog id -> tile/metadata, and back. catalogFor() returns 0 when the
// requested tile/metadata pair has no render state, so callers can detect a
// missing appearance instead of silently drawing the wrong block.
BlockState decode(uint16_t block);
uint16_t catalogFor(int tileId, int data);

// --- per-block queries ----------------------------------------------------
// Charge level a block emits, ignoring the direction of the reader. Dust
// reports its stored level; lever, pressed button and lit torch report
// SignalMax; an unpowered repeater reports none and a powered one reports
// SignalMax only along its facing.
int signalAt(int x, int y, int z, const BlockReader& blockAt);

// Charge a block delivers specifically to the given neighbour face
// (Facing: 0 down, 1 up, 2 north, 3 south, 4 west, 5 east). This is what
// enforces repeater one-way conduction.
int signalTo(int x, int y, int z, int facing, const BlockReader& blockAt);

// Strongest charge reaching a cell from its six neighbours.
int bestNeighbourSignal(int x, int y, int z, const BlockReader& blockAt);

// True when the tile is a charge emitter rather than a conductor.
bool isPowerSource(int tileId);

// Emitters among the six neighbours of a cell.
std::vector<PowerSource> powerSources(int x, int y, int z, const BlockReader& blockAt);

// --- propagation ----------------------------------------------------------
// Recompute dust charge over the wire network reachable from (x,y,z) and
// return the blocks whose appearance changed. Pure: nothing is written.
// Charge falls by exactly 1 per cell and is clamped to 0..15, so a branch
// inherits the same level as the cell it splits from. Repeaters are treated as
// boundaries, feeding their output cell at SignalMax and never conducting
// backwards.
std::vector<Change> propagate(int x, int y, int z, const BlockReader& blockAt);

// --- timing ---------------------------------------------------------------
// Output delay of a repeater in GAME TICKS. Metadata bits 2-3 select delay
// steps 1-4, and each step is two game ticks, so the result is 2/4/6/8.
// Verified against the upstream delay table and its doubling.
int repeaterDelayTicks(int data);

// Delay step 1-4 encoded in repeater metadata, for callers that want the
// displayed setting rather than the tick count.
int repeaterDelayStep(int data);

// Metadata with the delay stepped one position forward, wrapping 4 -> 1, which
// is what right-clicking a repeater does. Facing bits are preserved.
int repeaterCycleDelay(int data);

// Charge entering the BACK of the repeater at (x,y,z); this is the only side it
// accepts input from. Returns SignalNone when the block is not a repeater.
int repeaterInputSignal(int x, int y, int z, const BlockReader& blockAt);

// Tile id the repeater at (x,y,z) should settle to once its delay has elapsed.
// Pure: the caller owns the timer and decides when to apply this. Returns the
// current tile when the block is not a repeater.
int repeaterResolve(int x, int y, int z, const BlockReader& blockAt);

// Ticks a pressed button stays down before springing back. Wood and stone
// differ: wood 30, stone 20. Returns 0 for a tile that is not a button.
// NOTE: this module does NOT run the release; see the header note above.
int buttonReleaseDelayTicks(int tileId);

// --- interaction helpers (pure state transitions) -------------------------
// Metadata/tile after toggling a lever. Returns the input unchanged when the
// block is not a lever.
int leverToggle(int data);
bool leverPowered(int data);

// Metadata after pressing a button, and its pressed test.
int buttonPress(int data);
bool buttonPressed(int data);

// Tile id a redstone torch should become given whether its input is charged.
// The torch INVERTS: a charged input turns it off. Returns the input tile when
// it is not a torch.
int torchTileForInput(int tileId, bool inputPowered);
bool torchLit(int tileId);

// True when the block a torch at (x,y,z) is mounted on is charged, which is the
// input the torch inverts. False when the block is not a torch.
bool torchInputPowered(int x, int y, int z, const BlockReader& blockAt);

// Tile id the torch at (x,y,z) should settle to, given its current input. Pure:
// the caller decides when to apply it. Returns the current tile when the block
// is not a torch or already settled.
int torchResolve(int x, int y, int z, const BlockReader& blockAt);

} // namespace mcredstone
