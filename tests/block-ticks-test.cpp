/* Standalone host test (no SDK, renderer, UI or network):
   g++ -std=c++17 -O2 -Wall -Wextra -Ijsapi/src/MinecraftRenderer \
       -Ijsapi/build/generated tests/block-ticks-test.cpp \
       jsapi/src/MinecraftRenderer/BlockTicks.cpp \
       jsapi/src/MinecraftRenderer/Redstone.cpp -o /tmp/block-ticks-test \
       && /tmp/block-ticks-test

   Covers button auto-release timing (wood 30 / stone 20), repeater delay
   switching, redstone torch inversion delay, wheat and cocoa age advance,
   duplicate bookings not stacking, budget carry-over without loss or
   reordering, determinism over a dt sequence, rejection of invalid input,
   the queue capacity ceiling, and the JSON round-trip.
   independent implementation, behaviour verified against observed rules.
*/
#include "BlockTicks.hpp"
#include "LegacyCatalog.hpp"
#include "Redstone.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <stdexcept>
#include <tuple>
#include <vector>

using namespace mcticks;

namespace {

const char* stage = "";
int checks = 0;

void require(bool condition, const char* what)
{
    ++checks;
    if (condition) return;
    std::printf("FAIL [%s]: %s\n", stage, what);
    std::fflush(stdout);
    std::abort();
}

// A bare sparse grid. The scheduler never sees it; it only gets the read
// lambda, which is the whole point of the pure interface.
struct Grid {
    std::map<std::tuple<int, int, int>, uint16_t> cells;

    void put(int x, int y, int z, int tile, int data)
    {
        const uint16_t id = mcredstone::catalogFor(tile, data);
        if (!id) {
            std::printf("FAIL [%s]: catalog has no state for tile %d data %d\n", stage, tile, data);
            std::abort();
        }
        cells[{x, y, z}] = id;
    }

    void apply(const std::vector<Change>& changes)
    {
        for (const auto& c : changes) cells[{c.x, c.y, c.z}] = uint16_t(c.renderIndex);
    }

    mcredstone::BlockState state(int x, int y, int z) const
    {
        const auto found = cells.find({x, y, z});
        return mcredstone::decode(found == cells.end() ? uint16_t(0) : found->second);
    }

    BlockReader reader() const
    {
        return [this](int x, int y, int z) -> uint16_t {
            const auto found = cells.find({x, y, z});
            return found == cells.end() ? uint16_t(0) : found->second;
        };
    }
};

constexpr float TickSeconds = 1.0f / 20.0f;

// Step exactly one tick per call, committing as a real caller would.
std::vector<Change> stepOneTick(BlockTicks& ticks, Grid& grid, int budget = 64)
{
    auto changes = ticks.advance(TickSeconds, budget, grid.reader());
    grid.apply(changes);
    return changes;
}

// Fire at most one queued entry without advancing the clock, which makes the
// firing order directly observable.
std::vector<Change> ticksOneEntry(BlockTicks& ticks, Grid& grid)
{
    auto changes = ticks.advance(0.0f, 1, grid.reader());
    grid.apply(changes);
    return changes;
}

// --- button auto-release --------------------------------------------------
// Upstream ButtonTile::getTickDelay returns 30 when sensitive and 20 when not;
// WoodButtonTile passes sensitive=true and StoneButtonTile passes false.
void testButtonRelease(int tile, int expectedTicks, const char* name)
{
    stage = name;
    Grid grid;
    BlockTicks ticks;

    const int pressed = mcredstone::buttonPress(1);
    grid.put(4, 70, 9, tile, pressed);
    require(mcredstone::buttonPressed(grid.state(4, 70, 9).data), "button starts pressed");

    require(mcredstone::buttonReleaseDelayTicks(tile) == expectedTicks,
            "redstone module reports the upstream release delay");
    require(defaultDelayTicks(tile) == expectedTicks,
            "scheduler uses the same delay the redstone module reports");
    require(ticks.schedule(4, 70, 9, tile, expectedTicks), "release is accepted");

    // Nothing may happen before the final tick: this is the assertion that
    // catches an off-by-one or a wrong tick count.
    for (int t = 1; t < expectedTicks; ++t) {
        const auto changes = stepOneTick(ticks, grid);
        if (!changes.empty()) {
            std::printf("FAIL [%s]: button released early, on tick %d of %d\n", stage, t,
                        expectedTicks);
            std::abort();
        }
        require(mcredstone::buttonPressed(grid.state(4, 70, 9).data), "still pressed before due");
        require(ticks.pending() == 1, "entry still queued before due");
    }

    const auto changes = stepOneTick(ticks, grid);
    require(changes.size() == 1, "exactly one change on the due tick");
    require(ticks.tick() == uint64_t(expectedTicks), "clock advanced one tick per step");
    require(!mcredstone::buttonPressed(grid.state(4, 70, 9).data), "button sprang back");
    require(grid.state(4, 70, 9).tileId == tile, "button kept its material");
    require(grid.state(4, 70, 9).data == 1, "facing bits preserved on release");
    require(ticks.pending() == 0, "queue drained");

    // Single-shot: a released button is settled and produces nothing further.
    require(stepOneTick(ticks, grid).empty(), "no change after release");
}

void testRepeaterDelay()
{
    stage = "repeater delay";
    // Delay step 1-4 doubled: 2/4/6/8 ticks. Step bits are metadata 2-3.
    const int steps[4] = {0, 4, 8, 12};
    const int expected[4] = {2, 4, 6, 8};

    for (int s = 0; s < 4; ++s) {
        Grid grid;
        BlockTicks ticks;
        // A repeater facing north with a powered input behind it, currently off.
        const int data = steps[s];
        grid.put(0, 64, 0, mcredstone::TileRepeaterOff, data);
        grid.put(0, 64, 1, mcredstone::TileLever, mcredstone::leverToggle(0));

        require(mcredstone::repeaterDelayTicks(data) == expected[s],
                "repeater reports the doubled delay step");
        const int delay = mcredstone::repeaterDelayTicks(data);
        require(mcredstone::repeaterInputSignal(0, 64, 0, grid.reader()) > 0,
                "repeater sees a charged input");
        require(ticks.schedule(0, 64, 0, mcredstone::TileRepeaterOff, delay),
                "repeater switch is accepted");

        for (int t = 1; t < delay; ++t) {
            require(stepOneTick(ticks, grid).empty(), "repeater holds before its delay elapses");
            require(grid.state(0, 64, 0).tileId == mcredstone::TileRepeaterOff, "still off");
        }
        const auto changes = stepOneTick(ticks, grid);
        require(changes.size() == 1, "repeater switched on the due tick");
        require(grid.state(0, 64, 0).tileId == mcredstone::TileRepeaterOn, "repeater is now on");
        require(grid.state(0, 64, 0).data == data, "delay and facing bits preserved");
    }
}

void testTorchDelay()
{
    stage = "torch delay";
    Grid grid;
    BlockTicks ticks;
    // A lit torch on a block that is now charged: the torch inverts and goes
    // out, after the 2-tick delay upstream NotGateTile reports.
    grid.put(2, 64, 2, mcredstone::TileRedstoneTorchOn, 5);
    grid.put(2, 63, 2, mcredstone::TileLever, mcredstone::leverToggle(0));

    require(defaultDelayTicks(mcredstone::TileRedstoneTorchOn) == 2, "torch delay is 2 ticks");
    require(mcredstone::torchInputPowered(2, 64, 2, grid.reader()), "torch input is charged");
    require(ticks.schedule(2, 64, 2, mcredstone::TileRedstoneTorchOn, 2), "torch flip accepted");

    require(stepOneTick(ticks, grid).empty(), "torch holds on the first tick");
    require(grid.state(2, 64, 2).tileId == mcredstone::TileRedstoneTorchOn, "still lit");
    const auto changes = stepOneTick(ticks, grid);
    require(changes.size() == 1, "torch flipped on the second tick");
    require(grid.state(2, 64, 2).tileId == mcredstone::TileRedstoneTorchOff, "torch went out");
    require(grid.state(2, 64, 2).data == 5, "mount orientation preserved");
}

void testCropAges()
{
    stage = "crop ages";
    Grid grid;
    BlockTicks ticks;

    // Wheat: metadata 0..7, one step per scheduled tick, stopping at 7.
    grid.put(5, 64, 5, TileWheat, 0);
    for (int age = 0; age < 7; ++age) {
        require(ticks.schedule(5, 64, 5, TileWheat, 1), "wheat step accepted");
        const auto changes = stepOneTick(ticks, grid);
        require(changes.size() == 1, "wheat advanced one step");
        require(grid.state(5, 64, 5).data == age + 1, "wheat age incremented by one");
        require(grid.state(5, 64, 5).tileId == TileWheat, "still wheat");
    }
    require(ticks.schedule(5, 64, 5, TileWheat, 1), "ripe wheat may still be booked");
    require(stepOneTick(ticks, grid).empty(), "ripe wheat does not advance past 7");
    require(grid.state(5, 64, 5).data == 7, "ripe wheat stays at 7");
}

// A fired entry that has nothing to do must report nothing, rather than a
// change that rewrites the cell with the value it already holds. A redundant
// change would be committed, marking chunks dirty and re-saving for no reason.
void testNoRedundantChanges()
{
    stage = "settled blocks report nothing";
    Grid grid;
    BlockTicks ticks;

    // An already-released button has nothing to spring back from.
    grid.put(0, 64, 0, mcredstone::TileButtonStone, 1);
    require(!mcredstone::buttonPressed(grid.state(0, 64, 0).data), "button starts released");
    require(ticks.schedule(0, 64, 0, mcredstone::TileButtonStone, 20), "booked on a released button");

    // A repeater whose output already matches its input is settled.
    grid.put(0, 64, 4, mcredstone::TileRepeaterOff, 0);
    require(mcredstone::repeaterResolve(0, 64, 4, grid.reader()) == mcredstone::TileRepeaterOff,
            "unpowered repeater resolves to off");
    require(ticks.schedule(0, 64, 4, mcredstone::TileRepeaterOff, 20), "booked on a settled repeater");

    // A lit torch with an uncharged input is likewise settled.
    grid.put(0, 64, 8, mcredstone::TileRedstoneTorchOn, 5);
    require(!mcredstone::torchInputPowered(0, 64, 8, grid.reader()), "torch input is uncharged");
    require(mcredstone::torchResolve(0, 64, 8, grid.reader()) == mcredstone::TileRedstoneTorchOn,
            "unpowered torch resolves to lit");
    require(ticks.schedule(0, 64, 8, mcredstone::TileRedstoneTorchOn, 20), "booked on a settled torch");

    const auto before = grid.cells;
    for (int t = 1; t <= 20; ++t) {
        const auto changes = stepOneTick(ticks, grid);
        require(changes.empty(), "a settled block produces no change at all");
    }
    require(ticks.pending() == 0, "entries were still consumed, so the budget stays honest");
    require(grid.cells == before, "the world was not touched");
}

// An entry booked for a cell that has since been replaced must be dropped, not
// applied to whatever occupies the cell now.
void testStaleCells()
{
    stage = "stale cells";
    Grid grid;
    BlockTicks ticks;

    grid.put(0, 64, 0, mcredstone::TileButtonStone, mcredstone::buttonPress(1));
    grid.put(1, 64, 0, mcredstone::TileButtonStone, mcredstone::buttonPress(1));
    require(ticks.schedule(0, 64, 0, mcredstone::TileButtonStone, 20), "booked");
    require(ticks.schedule(1, 64, 0, mcredstone::TileButtonStone, 20), "booked");

    // The first button is mined away; the second is replaced by MID-GROWTH
    // wheat. The wheat matters: it is a tile this scheduler does drive and it
    // is not settled, so acting on the stale entry would really age it. A ripe
    // crop would not prove anything, because it has nothing to do either way.
    grid.cells[{0, 64, 0}] = 0;
    grid.put(1, 64, 0, TileWheat, 3);

    for (int t = 1; t < 20; ++t) require(stepOneTick(ticks, grid).empty(), "nothing due yet");
    const auto changes = stepOneTick(ticks, grid);
    require(changes.empty(), "neither stale entry acted on the new occupant");
    require(ticks.pending() == 0, "both stale entries were consumed");
    require(grid.state(0, 64, 0).tileId == 0, "the emptied cell stayed empty");
    require(grid.state(1, 64, 0).tileId == TileWheat && grid.state(1, 64, 0).data == 3,
            "the replacement crop was not aged by the button's stale entry");
}

void testNoStacking()
{
    stage = "duplicate bookings";
    Grid grid;
    BlockTicks ticks;
    grid.put(1, 64, 1, mcredstone::TileButtonWood, mcredstone::buttonPress(2));

    require(ticks.schedule(1, 64, 1, mcredstone::TileButtonWood, 30), "first booking accepted");
    require(!ticks.schedule(1, 64, 1, mcredstone::TileButtonWood, 30), "exact repeat refused");
    require(!ticks.schedule(1, 64, 1, mcredstone::TileButtonWood, 5),
            "repeat with a different delay refused, not re-timed");
    require(ticks.pending() == 1, "queue still holds exactly one entry");

    // A different cell is a different entry.
    grid.put(1, 64, 2, mcredstone::TileButtonWood, mcredstone::buttonPress(2));
    require(ticks.schedule(1, 64, 2, mcredstone::TileButtonWood, 30), "other cell accepted");
    require(ticks.pending() == 2, "two distinct cells queued");

    // Tile FAMILY, not exact id: upstream matches a diode against the other
    // diode, and a torch against the other torch.
    Grid pair;
    BlockTicks family;
    pair.put(0, 64, 0, mcredstone::TileRepeaterOff, 0);
    require(family.schedule(0, 64, 0, mcredstone::TileRepeaterOff, 2), "repeater-off booked");
    require(!family.schedule(0, 64, 0, mcredstone::TileRepeaterOn, 2),
            "repeater-on is the same pending update");
    require(family.schedule(0, 64, 1, mcredstone::TileRedstoneTorchOn, 2), "torch-on booked");
    require(!family.schedule(0, 64, 1, mcredstone::TileRedstoneTorchOff, 2),
            "torch-off is the same pending update");
    require(family.pending() == 2, "family collapsing kept both cells separate");

    // The single surviving entry still fires at its original time.
    for (int t = 1; t < 30; ++t) require(stepOneTick(ticks, grid).empty(), "no early release");
    require(stepOneTick(ticks, grid).size() == 2, "both buttons released on tick 30");
}

void testBudgetCarryOver()
{
    stage = "budget carry-over";
    Grid grid;
    BlockTicks ticks;

    // Ten buttons, all due on the same tick, booked in deliberately scrambled
    // order so a lost or reordered entry is visible.
    const int order[10] = {7, 2, 9, 0, 5, 3, 8, 1, 6, 4};
    for (int i = 0; i < 10; ++i) {
        const int x = order[i];
        grid.put(x, 64, 0, mcredstone::TileButtonStone, mcredstone::buttonPress(1));
        require(ticks.schedule(x, 64, 0, mcredstone::TileButtonStone, 20), "booking accepted");
    }
    require(ticks.pending() == 10, "all ten queued");

    for (int t = 1; t < 20; ++t) require(stepOneTick(ticks, grid, 3).empty(), "nothing due yet");

    // Budget 3 of 10 due. Time advances fully; the rest stay queued.
    auto first = ticks.advance(TickSeconds, 3, grid.reader());
    grid.apply(first);
    require(first.size() == 3, "budget honoured exactly");
    require(ticks.pending() == 7, "overdue remainder kept");
    require(ticks.tick() == 20, "clock advanced even though the budget ran out");
    require(first[0].x == 0 && first[1].x == 1 && first[2].x == 2,
            "fired in coordinate order, not insertion order");

    // Remainder continues, in order, with no duplicates and nothing skipped.
    std::vector<int> seen;
    for (const auto& c : first) seen.push_back(c.x);
    auto second = ticks.advance(0.0f, 4, grid.reader());
    grid.apply(second);
    require(second.size() == 4, "second slice honoured its budget");
    for (const auto& c : second) seen.push_back(c.x);
    require(seen[3] == 3 && seen[6] == 6, "order preserved across the budget boundary");

    auto third = ticks.advance(0.0f, 100, grid.reader());
    grid.apply(third);
    require(third.size() == 3, "final slice drained the rest");
    for (const auto& c : third) seen.push_back(c.x);
    require(ticks.pending() == 0, "queue empty");

    require(seen.size() == 10, "every entry fired exactly once");
    for (int i = 0; i < 10; ++i) require(seen[size_t(i)] == i, "no entry lost, duplicated or reordered");
    for (int i = 0; i < 10; ++i)
        require(!mcredstone::buttonPressed(grid.state(i, 64, 0).data), "every button released");

    // dueTick must outrank the coordinate. These two entries are booked so
    // that the LATER one has the SMALLER coordinate: if the sort ignores
    // dueTick, the not-yet-due entry sorts first, is found undue and blocks
    // the one that really is due.
    {
        Grid order;
        BlockTicks ot;
        order.put(9, 64, 0, mcredstone::TileButtonStone, mcredstone::buttonPress(1));
        order.put(0, 64, 0, mcredstone::TileButtonStone, mcredstone::buttonPress(1));
        require(ot.schedule(9, 64, 0, mcredstone::TileButtonStone, 20), "early+far booked");
        require(ot.schedule(0, 64, 0, mcredstone::TileButtonStone, 40), "late+near booked");

        for (int t = 1; t < 20; ++t) require(stepOneTick(ot, order).empty(), "neither due yet");
        const auto due = stepOneTick(ot, order);
        require(due.size() == 1, "the due entry fired despite a lower-coordinate entry waiting");
        require(due[0].x == 9, "dueTick outranks the coordinate in the firing order");
        require(ot.pending() == 1, "the not-yet-due entry is still waiting");

        for (int t = 21; t < 40; ++t) require(stepOneTick(ot, order).empty(), "still not due");
        const auto second = stepOneTick(ot, order);
        require(second.size() == 1 && second[0].x == 0, "the later entry fired at its own time");
    }

    // y and z must take part in the order too: entries that differ only in y,
    // or only in z, have to fire in a stable, coordinate-sorted sequence.
    for (int axis = 0; axis < 2; ++axis) {
        Grid order;
        BlockTicks ot;
        const int coords[6] = {40, 7, 64, 12, 55, 23};
        for (int c : coords) {
            const int y = axis == 0 ? c : 64;
            const int z = axis == 0 ? 3 : c;
            order.put(5, y, z, mcredstone::TileButtonStone, mcredstone::buttonPress(1));
            require(ot.schedule(5, y, z, mcredstone::TileButtonStone, 20), "booked");
        }
        // Reach the due tick without firing anything, so the clock is not a
        // variable while the order is being observed.
        for (int t = 1; t <= 20; ++t)
            require(ot.advance(TickSeconds, 0, order.reader()).empty(), "zero budget fires nothing");
        require(ot.tick() == 20 && ot.pending() == 6, "all six are now due and still queued");

        // One at a time, so the budget forces the order to be observable.
        std::vector<int> fired;
        for (int i = 0; i < 6; ++i) {
            const auto one = ticksOneEntry(ot, order);
            require(one.size() == 1, "exactly one entry per unit budget");
            fired.push_back(axis == 0 ? one[0].y : one[0].z);
        }
        std::vector<int> expected(coords, coords + 6);
        std::sort(expected.begin(), expected.end());
        require(fired == expected, axis == 0 ? "y participates in the firing order"
                                            : "z participates in the firing order");
    }

    // A zero budget must consume nothing while still advancing time.
    Grid zero;
    BlockTicks zeroTicks;
    zero.put(0, 64, 0, mcredstone::TileButtonStone, mcredstone::buttonPress(1));
    require(zeroTicks.schedule(0, 64, 0, mcredstone::TileButtonStone, 20), "booked");
    require(zeroTicks.advance(1.0f, 0, zero.reader()).empty(), "zero budget fires nothing");
    require(zeroTicks.pending() == 1, "entry retained under a zero budget");
    require(zeroTicks.tick() == 2, "dt clamped to 0.1s even with no budget");
}

void testDeterminism()
{
    stage = "determinism";
    // Same dt sequence, same budget, two independent runs, scrambled insertion
    // order in the second: results must match change for change.
    // Mixed frame times, including a zero frame and one past the 0.1 s clamp,
    // repeated until well beyond the longest delay booked below so that every
    // entry actually fires inside the run.
    std::vector<float> dts;
    for (int rep = 0; rep < 8; ++rep) {
        const float pattern[] = {0.016f, 0.05f, 0.0f, 0.033f, 0.2f, 0.1f, 0.007f, 0.05f};
        for (float dt : pattern) dts.push_back(dt);
    }
    const int xs[6] = {3, 1, 5, 0, 4, 2};

    auto run = [&](bool reversed) {
        Grid grid;
        BlockTicks ticks;
        for (int i = 0; i < 6; ++i) {
            const int x = reversed ? xs[5 - i] : xs[i];
            grid.put(x, 64, 0, mcredstone::TileButtonWood, mcredstone::buttonPress(1));
            ticks.schedule(x, 64, 0, mcredstone::TileButtonWood, 30);
            grid.put(x, 64, 4, TileWheat, 0);
            ticks.schedule(x, 64, 4, TileWheat, 12 + x);
        }
        std::vector<std::tuple<uint64_t, int, int, int, int>> log;
        for (float dt : dts) {
            // Budget deliberately smaller than the number of simultaneously
            // due entries, so carry-over is part of what must be reproducible.
            const auto changes = ticks.advance(dt, 5, grid.reader());
            grid.apply(changes);
            for (const auto& c : changes)
                log.emplace_back(ticks.tick(), c.x, c.y, c.z, c.renderIndex);
        }
        return std::make_pair(log, ticks.toJson());
    };

    const auto a = run(false);
    const auto b = run(true);
    require(!a.first.empty(), "the determinism run actually did work");
    require(a.first == b.first, "same dt sequence yields an identical change log");
    require(a.second == b.second, "and an identical scheduler state");

    // Repeating the identical run a third time must reproduce it exactly.
    const auto c = run(false);
    require(a.first == c.first, "a repeated run reproduces its own log");
}

void testRejection()
{
    stage = "invalid input";
    Grid grid;
    BlockTicks ticks;
    const auto reader = grid.reader();

    // Non-finite and negative dt.
    bool threw = false;
    try { ticks.advance(-0.01f, 10, reader); } catch (const std::invalid_argument&) { threw = true; }
    require(threw, "negative dt rejected");
    threw = false;
    try { ticks.advance(std::nanf(""), 10, reader); } catch (const std::invalid_argument&) { threw = true; }
    require(threw, "NaN dt rejected");
    threw = false;
    try { ticks.advance(std::numeric_limits<float>::infinity(), 10, reader); }
    catch (const std::invalid_argument&) { threw = true; }
    require(threw, "infinite dt rejected");
    threw = false;
    try { ticks.advance(0.05f, -1, reader); } catch (const std::invalid_argument&) { threw = true; }
    require(threw, "negative budget rejected");
    threw = false;
    try { ticks.advance(0.05f, 10, BlockReader {}); } catch (const std::invalid_argument&) { threw = true; }
    require(threw, "missing block reader rejected");
    require(ticks.tick() == 0, "a rejected advance did not move the clock");

    // Out-of-range coordinates.
    require(!ticks.schedule(0, -1, 0, mcredstone::TileButtonStone, 20), "y below the world refused");
    require(!ticks.schedule(0, 256, 0, mcredstone::TileButtonStone, 20), "y above the world refused");
    require(!ticks.schedule(1 << 23, 64, 0, mcredstone::TileButtonStone, 20), "x out of range refused");
    require(!ticks.schedule(0, 64, -(1 << 23) - 1, mcredstone::TileButtonStone, 20),
            "z out of range refused");

    // Out-of-range delays.
    require(!ticks.schedule(0, 64, 0, mcredstone::TileButtonStone, 0), "zero delay refused");
    require(!ticks.schedule(0, 64, 0, mcredstone::TileButtonStone, -5), "negative delay refused");
    require(!ticks.schedule(0, 64, 0, mcredstone::TileButtonStone, 72001), "huge delay refused");

    // Tiles this round does not drive. Each is refused, not silently queued.
    const int undriven[] = {0, 1, 70, 72, 6, 141, 142, 115, 127, 83, 104, 105, 60, 55, 69};
    for (int tile : undriven) {
        require(!driven(tile), "tile is correctly reported as not driven");
        require(!ticks.schedule(0, 64, 0, tile, 20), "undriven tile refused by schedule");
    }
    require(ticks.pending() == 0, "nothing was queued by any rejected call");

    // The catalog really is missing what the header claims is missing, so the
    // refusals above are justified rather than merely asserted.
    require(mcredstone::catalogFor(70, 1) == 0, "pressure plate pressed state absent");
    require(mcredstone::catalogFor(72, 1) == 0, "wood plate pressed state absent");
    require(mcredstone::catalogFor(6, 0) == 0, "sapling absent from the catalog");
    require(mcredstone::catalogFor(141, 1) == 0, "carrot metadata 1 absent");
    require(mcredstone::catalogFor(142, 1) == 0, "potato metadata 1 absent");
    require(mcredstone::catalogFor(115, 2) == 0, "nether wart metadata 2 absent");
    require(mcredstone::catalogFor(104, 0) == 0, "melon stem absent");
    require(mcredstone::catalogFor(105, 0) == 0, "pumpkin stem absent");
    // Cocoa has all three ages for direction 0 but nothing for the other three
    // mount orientations, so the age chain only exists for one facing.
    for (int age = 0; age <= 2; ++age)
        require(mcredstone::catalogFor(127, age << 2) != 0, "cocoa direction 0 ages exist");
    for (int dir = 1; dir <= 3; ++dir)
        require(mcredstone::catalogFor(127, dir) == 0, "cocoa other facings are absent");

    // And present for everything this round does drive.
    for (int d = 0; d <= 7; ++d)
        require(mcredstone::catalogFor(TileWheat, d) != 0, "every wheat age is present");
    for (int tile : {mcredstone::TileButtonStone, mcredstone::TileButtonWood})
        for (int face = 1; face <= 4; ++face) {
            require(mcredstone::catalogFor(tile, face) != 0, "button released state present");
            require(mcredstone::catalogFor(tile, face | 8) != 0, "button pressed state present");
        }
}

void testCapacity()
{
    stage = "capacity ceiling";
    BlockTicks ticks;
    const size_t cap = BlockTicks::Capacity;

    // Distinct cells, filling the queue to exactly the ceiling. A real pressed
    // button is placed for each, so firing produces a real change and the
    // budget accounting below is measuring work rather than dropped entries.
    Grid grid;
    size_t placed = 0;
    for (int y = 0; y < 256 && placed < cap; ++y)
        for (int x = 0; x < 64 && placed < cap; ++x) {
            grid.put(x, y, 0, mcredstone::TileButtonStone, mcredstone::buttonPress(1));
            if (!ticks.schedule(x, y, 0, mcredstone::TileButtonStone, 20)) {
                std::printf("FAIL [%s]: refused entry %zu below the ceiling\n", stage, placed);
                std::abort();
            }
            ++placed;
        }
    require(ticks.pending() == cap, "queue filled to exactly the ceiling");

    // Over the ceiling: refused explicitly, and nothing is evicted.
    require(!ticks.schedule(1000, 200, 7, mcredstone::TileButtonStone, 20),
            "booking past the ceiling is refused");
    require(ticks.pending() == cap, "refusal did not drop an existing entry");

    // Space freed by firing is reusable. dt is clamped to 0.1 s, i.e. two
    // ticks per call, so reaching the 20-tick due time takes ten calls; a zero
    // budget keeps the queue intact while the clock gets there.
    for (int i = 0; i < 10; ++i) require(ticks.advance(0.1f, 0, grid.reader()).empty(), "no budget");
    require(ticks.tick() == 20, "clock reached the due tick");
    const auto fired = ticks.advance(0.0f, 10, grid.reader());
    require(fired.size() == 10, "fired a budgeted slice");
    require(ticks.pending() == cap - 10, "ten slots freed");
    require(ticks.schedule(1000, 200, 7, mcredstone::TileButtonStone, 20),
            "a freed slot is reusable");
}

void testJsonRoundTrip()
{
    stage = "json round-trip";
    Grid grid;
    BlockTicks ticks;

    grid.put(3, 64, 7, mcredstone::TileButtonWood, mcredstone::buttonPress(1));
    grid.put(-9, 12, 40, mcredstone::TileButtonStone, mcredstone::buttonPress(2));
    grid.put(0, 64, 0, mcredstone::TileRepeaterOff, 8);
    grid.put(5, 64, 5, TileWheat, 3);
    grid.put(6, 64, 6, mcredstone::TileRedstoneTorchOn, 5);
    require(ticks.schedule(3, 64, 7, mcredstone::TileButtonWood, 30), "booked");
    require(ticks.schedule(-9, 12, 40, mcredstone::TileButtonStone, 20), "booked");
    require(ticks.schedule(0, 64, 0, mcredstone::TileRepeaterOff, 6), "booked");
    require(ticks.schedule(5, 64, 5, TileWheat, 11), "booked");
    require(ticks.schedule(6, 64, 6, mcredstone::TileRedstoneTorchOn, 2), "booked");

    // Advance past a whole number of ticks AND leave a fraction over, so both
    // the clock and the remainder are non-zero and both must survive the trip.
    ticks.advance(0.016f, 64, grid.reader());
    ticks.advance(0.1f, 64, grid.reader());
    ticks.advance(0.037f, 64, grid.reader());
    const std::string saved = ticks.toJson();

    require(ticks.tick() > 0, "the clock really advanced before saving");
    require(saved.find("\"tick\":" + std::to_string(ticks.tick())) != std::string::npos,
            "the saved document carries the current tick");

    BlockTicks restored;
    require(restored.fromJson(saved), "restore accepted its own output");
    require(restored.tick() == ticks.tick(), "the restored clock is the saved clock, not zero");
    require(restored.toJson() == saved, "re-serializing reproduces the document");
    require(restored.tick() == ticks.tick(), "clock restored");
    require(restored.pending() == ticks.pending(), "queue size restored");

    const auto before = ticks.pendingList();
    const auto after = restored.pendingList();
    for (size_t i = 0; i < before.size(); ++i) {
        require(before[i].x == after[i].x && before[i].y == after[i].y && before[i].z == after[i].z,
                "coordinate restored");
        require(before[i].tileId == after[i].tileId, "tile id restored");
        require(before[i].dueTick == after[i].dueTick, "due tick restored");
    }

    // A restored scheduler must behave identically from here on, including the
    // fractional remainder: that is what makes the round-trip meaningful.
    Grid gridA = grid, gridB = grid;
    const float dts[] = {0.021f, 0.05f, 0.1f, 0.034f, 0.4f, 0.05f};
    for (float dt : dts) {
        auto a = ticks.advance(dt, 3, gridA.reader());
        auto b = restored.advance(dt, 3, gridB.reader());
        gridA.apply(a);
        gridB.apply(b);
        require(a.size() == b.size(), "restored scheduler fires the same count");
        for (size_t i = 0; i < a.size(); ++i)
            require(a[i].x == b[i].x && a[i].y == b[i].y && a[i].z == b[i].z &&
                        a[i].renderIndex == b[i].renderIndex,
                    "restored scheduler produces identical changes");
    }
    require(ticks.toJson() == restored.toJson(), "states stay identical after replay");
    require(gridA.cells == gridB.cells, "both worlds ended identical");

    // The largest valid int64 is accepted exactly, which is the boundary the
    // hand-rolled digit loop guards. A due tick that far out simply never
    // arrives, so it must survive as a pending entry rather than be refused.
    {
        BlockTicks big;
        const std::string doc =
            "{\"version\":1,\"tick\":9223372036854775806,\"remainder\":0,"
            "\"ticksPerSecond\":20,\"pending\":"
            "[{\"x\":0,\"y\":0,\"z\":0,\"tileId\":77,\"dueTick\":9223372036854775807}]}";
        require(big.fromJson(doc), "int64 maximum accepted");
        require(big.tick() == 9223372036854775806ull, "large clock survived exactly");
        require(big.pendingList().at(0).dueTick == 9223372036854775807ull,
                "large due tick survived exactly");
        require(big.toJson() == doc, "large values re-serialize unchanged");
    }

    // A document holding exactly Capacity entries restores; one more is
    // refused. Without that check a save file could inflate the queue past the
    // ceiling that schedule() enforces.
    {
        std::string head = "{\"version\":1,\"tick\":0,\"remainder\":0,\"ticksPerSecond\":20,\"pending\":[";
        std::string body;
        size_t written = 0;
        for (int y = 0; y < 256 && written < BlockTicks::Capacity; ++y)
            for (int x = 0; x < 64 && written < BlockTicks::Capacity; ++x) {
                if (written) body += ',';
                body += "{\"x\":" + std::to_string(x) + ",\"y\":" + std::to_string(y) +
                        ",\"z\":0,\"tileId\":77,\"dueTick\":5}";
                ++written;
            }
        require(written == BlockTicks::Capacity, "built a full-capacity document");

        BlockTicks full;
        require(full.fromJson(head + body + "]}"), "a full-capacity document restores");
        require(full.pending() == BlockTicks::Capacity, "every entry restored");

        BlockTicks over;
        const std::string extra = ",{\"x\":999,\"y\":200,\"z\":7,\"tileId\":77,\"dueTick\":5}";
        require(!over.fromJson(head + body + extra + "]}"),
                "a document past the ceiling is refused");
        require(over.pending() == 0, "the refused oversized document left nothing behind");
    }

    // Empty queue round-trips.
    BlockTicks empty;
    BlockTicks emptyBack;
    require(emptyBack.fromJson(empty.toJson()), "empty state restores");
    require(emptyBack.pending() == 0 && emptyBack.tick() == 0, "empty state is empty");

    // Malformed, out-of-range and semantically invalid documents are refused,
    // and a refused restore leaves the target untouched.
    BlockTicks guard;
    require(guard.schedule(1, 64, 1, mcredstone::TileButtonWood, 30), "guard booked");
    const std::string guarded = guard.toJson();
    const char* bad[] = {
        "",
        "{}",
        "not json",
        "{\"version\":2,\"tick\":0,\"remainder\":0,\"ticksPerSecond\":20,\"pending\":[]}",
        "{\"version\":1,\"tick\":-1,\"remainder\":0,\"ticksPerSecond\":20,\"pending\":[]}",
        "{\"version\":1,\"tick\":0,\"remainder\":1.5,\"ticksPerSecond\":20,\"pending\":[]}",
        "{\"version\":1,\"tick\":0,\"remainder\":0,\"ticksPerSecond\":30,\"pending\":[]}",
        // tile 70 is a pressure plate: not driven, so not restorable either.
        "{\"version\":1,\"tick\":0,\"remainder\":0,\"ticksPerSecond\":20,\"pending\":"
        "[{\"x\":0,\"y\":0,\"z\":0,\"tileId\":70,\"dueTick\":5}]}",
        // y outside the world column.
        "{\"version\":1,\"tick\":0,\"remainder\":0,\"ticksPerSecond\":20,\"pending\":"
        "[{\"x\":0,\"y\":999,\"z\":0,\"tileId\":77,\"dueTick\":5}]}",
        // the same cell twice.
        "{\"version\":1,\"tick\":0,\"remainder\":0,\"ticksPerSecond\":20,\"pending\":"
        "[{\"x\":0,\"y\":0,\"z\":0,\"tileId\":77,\"dueTick\":5},"
        "{\"x\":0,\"y\":0,\"z\":0,\"tileId\":77,\"dueTick\":9}]}",
        // trailing junk after a valid document.
        "{\"version\":1,\"tick\":0,\"remainder\":0,\"ticksPerSecond\":20,\"pending\":[]} extra",
        // Entries that are individually valid, but the document fails AFTER
        // the list. The restore must not have installed them already.
        "{\"version\":1,\"tick\":0,\"remainder\":0,\"ticksPerSecond\":20,\"pending\":"
        "[{\"x\":4,\"y\":70,\"z\":9,\"tileId\":77,\"dueTick\":5}]} trailing",
        "{\"version\":1,\"tick\":0,\"remainder\":0,\"ticksPerSecond\":20,\"pending\":"
        "[{\"x\":4,\"y\":70,\"z\":9,\"tileId\":77,\"dueTick\":5}]",
        // A valid first entry followed by an invalid second one: neither may land.
        "{\"version\":1,\"tick\":0,\"remainder\":0,\"ticksPerSecond\":20,\"pending\":"
        "[{\"x\":4,\"y\":70,\"z\":9,\"tileId\":77,\"dueTick\":5},"
        "{\"x\":0,\"y\":0,\"z\":0,\"tileId\":70,\"dueTick\":6}]}",
        // truncated.
        "{\"version\":1,\"tick\":0,\"remainder\":0,\"ticksPerSecond\":20,\"pending\":[",
        // Integer fields that overflow int64. The reader parses digits itself
        // rather than calling std::stoll, so overflow must be refused here and
        // not wrap around into a plausible-looking small number.
        "{\"version\":1,\"tick\":99999999999999999999,\"remainder\":0,"
        "\"ticksPerSecond\":20,\"pending\":[]}",
        "{\"version\":1,\"tick\":0,\"remainder\":0,\"ticksPerSecond\":20,\"pending\":"
        "[{\"x\":0,\"y\":0,\"z\":0,\"tileId\":77,\"dueTick\":99999999999999999999}]}",
        // A non-numeric integer field.
        "{\"version\":1,\"tick\":abc,\"remainder\":0,\"ticksPerSecond\":20,\"pending\":[]}",
        // A sign with no digits behind it.
        "{\"version\":1,\"tick\":-,\"remainder\":0,\"ticksPerSecond\":20,\"pending\":[]}",
    };
    for (const char* doc : bad) {
        require(!guard.fromJson(doc), "malformed or invalid document refused");
        require(guard.toJson() == guarded, "a refused restore changed nothing");
    }
}

} // namespace

int main()
{
    testButtonRelease(mcredstone::TileButtonWood, 30, "wood button release");
    testButtonRelease(mcredstone::TileButtonStone, 20, "stone button release");
    testRepeaterDelay();
    testTorchDelay();
    testCropAges();
    testNoRedundantChanges();
    testStaleCells();
    testNoStacking();
    testBudgetCarryOver();
    testDeterminism();
    testRejection();
    testCapacity();
    testJsonRoundTrip();

    std::printf("block-ticks-test: OK (%d checks)\n", checks);
    return 0;
}
