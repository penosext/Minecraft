#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Scheduled block updates: the "tick next tick" queue.
//
// independent implementation, behaviour verified against observed rules.
//
// This module is a PURE SCHEDULER. It never holds a world, never reads one by
// itself and never writes one. Callers hand advance() a block-read callback and
// atomically commit the returned changes (OriginalWorld::setBlocks validates the
// whole batch before publishing, so a rejected batch never lands half-applied).
// Values exchanged with the caller are CURRENT catalog ids
// (legacy::kBlockSpecs index + 1, 0 == air), the same convention used by
// World/OriginalWorld/Redstone/BlockCollision.
//
// Time base: 20 ticks per second, with the caller's dt clamped to 0.1 s and
// accumulated in a fractional remainder, matching the fixed-step loop Scene
// already runs. A tick is exactly 0.05 s; it is not frame-count based.
//
// --- what this round actually DRIVES -------------------------------------
// Each of these was checked against the upstream tile that owns the delay AND
// against the render states the generated catalog really contains. A behaviour
// is only listed here when both the tick count and every appearance it needs
// are present; nothing below substitutes a wrong-looking block.
//
//   * Button auto-release. Stone 20 ticks, wood 30. This closes the gap
//     Redstone.hpp documented: buttonReleaseDelayTicks() reported the duration
//     but nothing advanced time, so a pressed button stayed down forever.
//   * Repeater (upstream "diode") output switching after its delay, 2/4/6/8
//     ticks, read from the existing mcredstone::repeaterDelayTicks().
//   * Redstone torch inversion after its 2-tick delay.
//   * Wheat age advance, metadata 0 -> 7 one step per scheduled tick. All
//     eight appearances are in the catalog, so every step can be shown.
//
// --- deliberately NOT implemented, and not faked -------------------------
// General gaps, none of which this module pretends to cover:
//   * Block entities of every kind: chests, furnaces, signs, dispensers,
//     hoppers, pistons, note blocks, spawners. There is no block-entity model
//     to tick, so none of their timers exist here.
//   * RANDOM TICKS. This queue is strictly deterministic and strictly
//     scheduled: an entry fires once, at the tick it was booked for. Upstream
//     drives crop growth, leaf decay, grass spread, ice/snow melt, farmland
//     moisture and nether wart from the random tick selector with a
//     probability roll per block per tick. None of that happens here. The crop
//     ages above only advance when the CALLER schedules them, and never on
//     their own.
//   * Fire spread and burn-out.
//   * Fluid flow, levelling and source removal for water and lava.
//   * Entity interaction of any kind: nothing stands on, walks into, bumps or
//     collides with a scheduled block, and no entity is spawned or consumed.
//   * Rescheduling. An entry is single-shot. Upstream tiles re-book themselves
//     from inside their own tick; here the caller owns that decision, so a
//     crop stops after one step and a repeater does not chase a changing input.
//   * Light updates, neighbour-notification cascades, and the
//     quasi-connectivity and update-order quirks of the original scheduler.
//   * Torch burn-out, and repeater locking by a side-on repeater, which
//     Redstone.hpp already lists as absent.
//
// Behaviours skipped specifically because the generated catalog has no render
// state for them. Driving these would mean drawing a block that does not
// exist, so they are refused by schedule() instead:
//   * Pressure plates (tiles 70, 72). Only the released appearance
//     (metadata 0) is in the catalog; the pressed state, metadata 1, is
//     absent, so neither pressing nor releasing can be shown.
//   * Saplings (tile 6). The catalog has no sapling state at all, and this
//     directory has no tree/structure generation entry point, so neither
//     "grows into a tree" nor "disappears on expiry" is possible. No tree is
//     grown, and none is faked.
//   * Carrots (tile 141) and potatoes (tile 142). Upstream steps metadata by
//     one up to 7, but the catalog only holds metadata 0, 2, 4 and 7 (four
//     textures, not four consecutive ages). Metadata 1, 3, 5 and 6 have no
//     appearance, so a per-age advance breaks at the very first step.
//   * Nether wart (tile 115). Metadata 0, 1 and 3 exist but metadata 2 is
//     missing, so the age chain is broken in the middle.
//   * Cocoa (tile 127). Metadata is age << 2 | direction, and the catalog has
//     all three ages for direction 0 only (metadata 0, 4 and 8). A cocoa pod
//     mounted on any of the other three sides has no appearance at any age, so
//     ageing one would have to either redraw it facing the wrong way or stall.
//   * Sugar cane / reeds (tile 83). One state only, with no height or age.
//   * Melon and pumpkin stems (tiles 104, 105). No stem state of any age; the
//     catalog has only the ripe 'pumpkin' body.
//   * Farmland moisture (tile 60). Only the dry and fully wet states exist
//     (metadata 0 and 7), so moisture could only ever be a two-valued flip,
//     never the 0..7 ramp upstream keeps. Not driven here.
namespace mcticks {

// The one non-redstone tile this scheduler accepts, matching the upstream
// numbering the catalog is keyed on. Redstone.hpp owns the redstone ids.
constexpr int TileWheat = 59;

// A queued update. dueTick is an absolute tick on this scheduler's clock.
struct Scheduled {
    int x = 0, y = 0, z = 0;
    int tileId = 0;
    uint64_t dueTick = 0;
};

// A single block change the caller should commit. renderIndex is a CURRENT
// catalog id (index + 1), directly storable via World/OriginalWorld setBlock
// or, for a whole batch, OriginalWorld::setBlocks.
struct Change {
    int x = 0, y = 0, z = 0, renderIndex = 0;
};

using BlockReader = std::function<uint16_t(int, int, int)>;

class BlockTicks {
public:
    // Ticks per second, and the dt clamp, both shared with Scene's fixed step.
    static constexpr int TicksPerSecond = 20;
    static constexpr float MaxFrameSeconds = 0.1f;

    // Queue ceiling. schedule() returns false once this many entries are
    // waiting: a refusal the caller can see, never a silent drop.
    static constexpr size_t Capacity = 8192;

    // Accepted delay range, in game ticks. One hour of ticks is far beyond any
    // delay the driven tiles use (the longest is a 30-tick wood button).
    static constexpr int MinDelayTicks = 1;
    static constexpr int MaxDelayTicks = 72000;

    // Coordinate limits. y matches the world column height; the horizontal
    // bound only keeps the queue key well away from overflow. The real world
    // extent is the caller's to enforce, since a pure scheduler cannot know it.
    static constexpr int WorldHeight = 256;
    static constexpr int MaxHorizontal = 1 << 23;

    // Book one update for (x,y,z), to fire delayTicks from now.
    //
    // Returns false, and changes nothing, when:
    //   * tileId is not one of the behaviours listed at the top of this header
    //     (so pressure plates, saplings, carrots, potatoes, nether wart,
    //     cocoa, reeds and stems are refused rather than half-driven),
    //   * the coordinate or delay is out of range,
    //   * the queue is already at Capacity,
    //   * or this cell already has an equivalent update queued.
    //
    // Repeat bookings do NOT stack, matching upstream: a cell already in the
    // queue is ignored. Equivalence is per cell and per tile FAMILY, not per
    // exact id, again as upstream: repeater-off and repeater-on count as the
    // same pending update, and so do torch-off and torch-on.
    bool schedule(int x, int y, int z, int tileId, int delayTicks);

    // Advance the clock by dt seconds and fire whatever is due.
    //
    // budget caps how many entries are consumed in this call, so a large queue
    // cannot stall a frame. Entries left over keep their place and fire on a
    // later call, in the same order: nothing is dropped and nothing is
    // reordered. Note that time still advances fully even when the budget runs
    // out, so skipped entries are simply overdue, not rescheduled.
    //
    // Firing order is by dueTick, then by x, y, z, then tile id. That is a
    // total order over the queue, which makes the result depend only on the
    // sequence of dt values and not on insertion history. Upstream instead
    // breaks ties by insertion sequence; this is a deliberate choice in favour
    // of a reproducible result.
    //
    // An entry is consumed when it fires even if it produces no change (the
    // block was replaced, or is already settled), which is what keeps the
    // budget an honest bound on work. The returned changes are the caller's to
    // commit, atomically; this object has already moved past them.
    //
    // Throws std::invalid_argument when dt is negative or not finite, when
    // budget is negative, or when blockAt is empty.
    std::vector<Change> advance(float dt, int budget, const BlockReader& blockAt);

    // Current absolute tick.
    uint64_t tick() const { return tick_; }

    // Entries still waiting, due or not.
    size_t pending() const { return queue_.size(); }

    // The waiting entries, in the order advance() would fire them.
    std::vector<Scheduled> pendingList() const;

    // Drop every entry and reset the clock and the fractional remainder.
    void clear();

    // --- save round-trip ---------------------------------------------------
    // Serialize the clock, the fractional remainder and the whole queue. This
    // module does no file I/O; wiring it into the save format is the caller's
    // job.
    std::string toJson() const;

    // Restore from toJson() output. All-or-nothing: on malformed input, an
    // out-of-range field, an unsupported tile id, a duplicate cell or more
    // than Capacity entries, this returns false and leaves the object exactly
    // as it was. A restored entry whose dueTick is already in the past is
    // accepted and fires on the next advance().
    bool fromJson(const std::string& json);

private:
    std::vector<Scheduled> queue_;
    uint64_t tick_ = 0;
    double remainder_ = 0.0;
};

// True when this scheduler can drive the tile, i.e. the upstream delay is known
// AND every appearance the behaviour needs is in the catalog. Exposed so a
// caller can check before booking instead of reading a false from schedule().
bool driven(int tileId);

// Delay in game ticks this scheduler would use for the tile, or 0 when it is
// not driven. Crops have no intrinsic delay upstream (they grow on random
// ticks, which this module does not run), so they report 0 and the caller
// chooses the delay it passes to schedule().
int defaultDelayTicks(int tileId);

} // namespace mcticks
