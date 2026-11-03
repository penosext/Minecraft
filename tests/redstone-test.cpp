/* Standalone host test (no SDK, renderer, UI or network):
   g++ -std=c++17 -O2 -Wall -Wextra -Ijsapi/src/MinecraftRenderer \
       -Ijsapi/build/generated tests/redstone-test.cpp \
       jsapi/src/MinecraftRenderer/Redstone.cpp -o /tmp/redstone-test \
       && /tmp/redstone-test

   Covers wire decay, branching, repeater delay and one-way conduction, lever
   toggling, button release timing, and redstone torch inversion.
   independent implementation, rules verified against observed behaviour.
*/
#include "Redstone.hpp"
#include "LegacyCatalog.hpp"

#include <cassert>
#include <cstdio>
#include <map>
#include <tuple>
#include <vector>

using namespace mcredstone;

namespace {

const char* stage = "";

void require(bool condition, const char* what)
{
    if (condition) return;
    std::printf("FAIL [%s]: %s\n", stage, what);
    std::fflush(stdout);
    std::abort();
}

// A bare sparse grid. The module never sees it; it only gets the read lambda,
// which is the whole point of the pure interface.
struct Grid {
    std::map<std::tuple<int, int, int>, uint16_t> cells;

    void put(int x, int y, int z, int tile, int data)
    {
        const uint16_t id = catalogFor(tile, data);
        require(id != 0, "fixture needs a render state that exists in the catalog");
        cells[{x, y, z}] = id;
    }
    void clear() { cells.clear(); }
    BlockReader reader() const
    {
        return [this](int x, int y, int z) -> uint16_t {
            const auto found = cells.find({x, y, z});
            return found == cells.end() ? uint16_t(0) : found->second;
        };
    }
    int dataAt(int x, int y, int z) const
    {
        const auto found = cells.find({x, y, z});
        return found == cells.end() ? -1 : decode(found->second).data;
    }
};

int changeAt(const std::vector<Change>& changes, int x, int y, int z)
{
    for (const auto& c : changes)
        if (c.x == x && c.y == y && c.z == z) return decode(uint16_t(c.renderIndex)).data;
    return -1;
}

// ---------------------------------------------------------------- catalog ---
// The powered appearances must actually exist, otherwise the logic below would
// be computing states that can never be drawn.
void renderStates()
{
    stage = "render-states";
    for (int d = 0; d < 8; ++d) {
        require(catalogFor(TileLever, d) != 0, "lever unpowered orientation missing");
        require(catalogFor(TileLever, d | 8) != 0, "lever POWERED orientation missing");
    }
    for (int d = 1; d < 5; ++d) {
        require(catalogFor(TileButtonStone, d) != 0, "stone button released state missing");
        require(catalogFor(TileButtonStone, d | 8) != 0, "stone button PRESSED state missing");
        require(catalogFor(TileButtonWood, d) != 0, "wood button released state missing");
        require(catalogFor(TileButtonWood, d | 8) != 0, "wood button PRESSED state missing");
    }
    for (int facing = 0; facing < 4; ++facing)
        for (int step = 0; step < 4; ++step) {
            const int data = (step << 2) | facing;
            require(catalogFor(TileRepeaterOff, data) != 0, "repeater off state missing");
            require(catalogFor(TileRepeaterOn, data) != 0, "repeater ON state missing");
        }
    for (int level = 0; level <= 15; ++level)
        require(catalogFor(TileRedstoneDust, level) != 0, "dust charge level missing");
    for (int d = 1; d < 6; ++d) {
        require(catalogFor(TileRedstoneTorchOff, d) != 0, "torch off state missing");
        require(catalogFor(TileRedstoneTorchOn, d) != 0, "torch on state missing");
    }
    // Charge level must be visually distinguishable. The two dust overlay
    // sprites in the atlas are fully transparent, so the level shows up as a
    // colour ramp; unlit and fully lit must not share a colour.
    const uint16_t unlit = catalogFor(TileRedstoneDust, 0);
    const uint16_t lit = catalogFor(TileRedstoneDust, 15);
    const uint32_t unlitColour = legacy::kBlockSpecs[unlit - 1].color;
    const uint32_t litColour = legacy::kBlockSpecs[lit - 1].color;
    require(unlitColour != litColour, "unpowered and fully charged dust look identical");
    uint32_t previous = 0;
    for (int level = 1; level <= 15; ++level) {
        const uint32_t colour = legacy::kBlockSpecs[catalogFor(TileRedstoneDust, level) - 1].color;
        require(colour > previous, "dust charge colours must increase monotonically");
        previous = colour;
    }
    // Round tripping guards against two states colliding on one appearance.
    for (int d = 0; d < 16; ++d) {
        const uint16_t id = catalogFor(TileRepeaterOn, d);
        if (!id) continue;
        const BlockState back = decode(id);
        require(back.tileId == TileRepeaterOn && back.data == d, "repeater state does not round trip");
    }
    require(decode(0).tileId == 0, "id 0 must decode as air");
    require(catalogFor(TileRedstoneDust, 16) == 0, "out of range metadata must not resolve");
    require(catalogFor(TileRedstoneDust, -1) == 0, "negative metadata must not resolve");
}

// ------------------------------------------------------------------- wire ---
void wireDecay()
{
    stage = "wire-decay";
    Grid grid;
    // Lever on the -X wall (data 1) feeding a 20 long dust run along +X.
    grid.put(0, 0, 0, TileLever, 1 | 8);
    for (int x = 1; x <= 20; ++x) grid.put(x, 0, 0, TileRedstoneDust, 0);
    const auto changes = propagate(1, 0, 0, grid.reader());

    // Charge falls by exactly one per cell from the source maximum.
    require(changeAt(changes, 1, 0, 0) == 15, "cell next to a lever must carry 15");
    for (int x = 2; x <= 15; ++x) {
        const int expected = 15 - (x - 1);
        require(changeAt(changes, x, 0, 0) == expected, "wire must lose exactly one level per cell");
    }
    // 15 cells of travel exhausts the signal; nothing beyond stays lit.
    require(changeAt(changes, 16, 0, 0) == -1, "charge must not reach past level 0");
    for (int x = 17; x <= 20; ++x)
        require(changeAt(changes, x, 0, 0) == -1, "far wire must stay unpowered");

    // An unpowered lever leaves the whole run dark.
    grid.clear();
    grid.put(0, 0, 0, TileLever, 1);
    for (int x = 1; x <= 5; ++x) grid.put(x, 0, 0, TileRedstoneDust, 0);
    require(propagate(1, 0, 0, grid.reader()).empty(), "unpowered lever must not charge the wire");
}

void wireBranching()
{
    stage = "wire-branch";
    Grid grid;
    // A T junction: trunk along +X, splitting at x=3 into both Z directions.
    grid.put(0, 0, 0, TileLever, 1 | 8);
    for (int x = 1; x <= 3; ++x) grid.put(x, 0, 0, TileRedstoneDust, 0);
    for (int z = 1; z <= 4; ++z) {
        grid.put(3, 0, z, TileRedstoneDust, 0);
        grid.put(3, 0, -z, TileRedstoneDust, 0);
    }
    const auto changes = propagate(1, 0, 0, grid.reader());
    require(changeAt(changes, 3, 0, 0) == 13, "junction cell level");
    // Both branches inherit the junction level and then decay independently;
    // splitting does not halve or otherwise divide the charge.
    for (int z = 1; z <= 4; ++z) {
        require(changeAt(changes, 3, 0, z) == 13 - z, "+Z branch decay");
        require(changeAt(changes, 3, 0, -z) == 13 - z, "-Z branch decay");
    }

    // Two sources feeding one wire: the stronger one wins at every cell.
    grid.clear();
    grid.put(0, 0, 0, TileLever, 1 | 8);
    for (int x = 1; x <= 9; ++x) grid.put(x, 0, 0, TileRedstoneDust, 0);
    grid.put(10, 0, 0, TileLever, 2 | 8);
    const auto both = propagate(5, 0, 0, grid.reader());
    require(changeAt(both, 1, 0, 0) == 15 && changeAt(both, 9, 0, 0) == 15, "both ends fully charged");
    require(changeAt(both, 5, 0, 0) == 11, "middle takes the stronger of the two sources");

    // A wire climbing one block stays connected.
    grid.clear();
    grid.put(0, 0, 0, TileLever, 1 | 8);
    grid.put(1, 0, 0, TileRedstoneDust, 0);
    grid.put(2, 1, 0, TileRedstoneDust, 0);
    const auto climb = propagate(1, 0, 0, grid.reader());
    require(changeAt(climb, 2, 1, 0) == 14, "wire must conduct one step upwards");
}

// --------------------------------------------------------------- repeater ---
void repeaterDelay()
{
    stage = "repeater-delay";
    // Delay steps 1-4 are two game ticks each.
    const int expected[4] = {2, 4, 6, 8};
    for (int step = 0; step < 4; ++step) {
        const int data = step << 2;
        require(repeaterDelayStep(data) == step + 1, "delay step must be 1-4");
        require(repeaterDelayTicks(data) == expected[step], "delay must be step*2 game ticks");
        // Facing bits must not disturb the delay reading.
        for (int facing = 0; facing < 4; ++facing)
            require(repeaterDelayTicks(data | facing) == expected[step], "facing must not change delay");
    }
    // Right-clicking cycles 1->2->3->4->1 and preserves facing.
    int data = 2; // facing 2, delay step 1
    const int seen[4] = {4, 6, 8, 2};
    for (int i = 0; i < 4; ++i) {
        data = repeaterCycleDelay(data);
        require((data & 3) == 2, "cycling delay must preserve facing");
        require(repeaterDelayTicks(data) == seen[i], "delay must cycle through 2/4/6/8");
    }
}

void repeaterOneWay()
{
    stage = "repeater-one-way";
    Grid grid;
    // Repeater at the origin with its input side at +Z (direction 0), so it
    // drives -Z. Wire on both sides.
    grid.put(0, 0, 1, TileRedstoneDust, 12);
    grid.put(0, 0, 0, TileRepeaterOn, 0);
    grid.put(0, 0, -1, TileRedstoneDust, 0);
    const auto reader = grid.reader();

    // Output is full strength regardless of how weak the input was, and only
    // leaves through the front face.
    require(signalTo(0, 0, 0, 2 /*north*/, reader) == 15, "repeater must drive its front at full strength");
    require(signalTo(0, 0, 0, 3 /*south*/, reader) == 0, "repeater must not drive backwards");
    require(signalTo(0, 0, 0, 4 /*west*/, reader) == 0, "repeater must not drive sideways");
    require(signalTo(0, 0, 0, 5 /*east*/, reader) == 0, "repeater must not drive sideways");

    // The wire in front is re-energised to 15, not to the input level.
    const auto forward = propagate(0, 0, -1, reader);
    require(changeAt(forward, 0, 0, -1) == 15, "repeater output restores full strength");

    // Driving the repeater from the front must not light the wire behind it.
    Grid reverse;
    reverse.put(0, 0, -1, TileLever, 1 | 8);      // source in FRONT of the repeater
    reverse.put(0, 0, 0, TileRepeaterOff, 0);
    reverse.put(0, 0, 1, TileRedstoneDust, 0);    // wire behind it
    const auto back = propagate(0, 0, 1, reverse.reader());
    require(changeAt(back, 0, 0, 1) == -1, "repeater must block reverse conduction");

    // An unpowered repeater emits nothing in any direction.
    Grid off;
    off.put(0, 0, 0, TileRepeaterOff, 0);
    for (int facing = 0; facing < 6; ++facing)
        require(signalTo(0, 0, 0, facing, off.reader()) == 0, "unpowered repeater must be silent");

    // Input is read only from the back face.
    Grid input;
    input.put(0, 0, 1, TileLever, 1 | 8);
    input.put(0, 0, 0, TileRepeaterOff, 0);
    require(repeaterInputSignal(0, 0, 0, input.reader()) == 15, "repeater must read its back input");
    require(repeaterResolve(0, 0, 0, input.reader()) == TileRepeaterOn, "charged repeater must settle on");

    Grid side;
    side.put(1, 0, 0, TileLever, 1 | 8);  // beside, not behind
    side.put(0, 0, 0, TileRepeaterOff, 0);
    require(repeaterInputSignal(0, 0, 0, side.reader()) == 0, "repeater must ignore side input");
    require(repeaterResolve(0, 0, 0, side.reader()) == TileRepeaterOff, "uncharged repeater stays off");

    // Weak dust behind the repeater still triggers it, and the output is full.
    Grid weak;
    weak.put(0, 0, 1, TileRedstoneDust, 1);
    weak.put(0, 0, 0, TileRepeaterOff, 0);
    require(repeaterInputSignal(0, 0, 0, weak.reader()) == 1, "dust feeds the repeater at its own level");
    require(repeaterResolve(0, 0, 0, weak.reader()) == TileRepeaterOn, "any nonzero input turns it on");
}

// ------------------------------------------------------------------ lever ---
void leverSwitch()
{
    stage = "lever";
    // Toggling flips bit 8 and never disturbs the mount orientation.
    for (int d = 0; d < 8; ++d) {
        require(!leverPowered(d), "fresh lever is unpowered");
        const int on = leverToggle(d);
        require(leverPowered(on) && (on & 7) == d, "toggle must power the lever and keep orientation");
        const int off = leverToggle(on);
        require(!leverPowered(off) && off == d, "toggling twice must return the original state");
    }
    // A powered lever is a full-strength source.
    Grid grid;
    grid.put(0, 0, 0, TileLever, 1 | 8);
    require(signalAt(0, 0, 0, grid.reader()) == 15, "powered lever emits maximum charge");
    require(isPowerSource(TileLever), "lever must be a charge source");
    grid.clear();
    grid.put(0, 0, 0, TileLever, 1);
    require(signalAt(0, 0, 0, grid.reader()) == 0, "unpowered lever emits nothing");

    // The lever does not drive the block it is mounted on; data 1 sits on the
    // -X wall, so no charge leaves through the west face.
    Grid mounted;
    mounted.put(0, 0, 0, TileLever, 1 | 8);
    require(bestNeighbourSignal(1, 0, 0, mounted.reader()) == 15, "cell beside a lever is charged");
    const auto sources = powerSources(1, 0, 0, mounted.reader());
    require(sources.size() == 1 && sources[0].level == 15, "lever must be reported as a source");
}

// ----------------------------------------------------------------- button ---
void buttonRelease()
{
    stage = "button";
    // Wood and stone differ only in how long they stay down.
    require(buttonReleaseDelayTicks(TileButtonWood) == 30, "wood button holds for 30 ticks");
    require(buttonReleaseDelayTicks(TileButtonStone) == 20, "stone button holds for 20 ticks");
    require(buttonReleaseDelayTicks(TileLever) == 0, "a lever has no release delay");
    require(buttonReleaseDelayTicks(TileRedstoneDust) == 0, "dust has no release delay");

    for (int d = 1; d < 5; ++d) {
        require(!buttonPressed(d), "fresh button is released");
        const int pressed = buttonPress(d);
        require(buttonPressed(pressed) && (pressed & 7) == d, "pressing keeps the orientation");
        require(buttonPress(pressed) == pressed, "pressing a held button changes nothing");
    }

    // A pressed button of either material is a full-strength source, and a
    // released one is silent.
    for (int tile : {TileButtonStone, TileButtonWood}) {
        Grid grid;
        grid.put(0, 0, 0, tile, 1 | 8);
        require(signalAt(0, 0, 0, grid.reader()) == 15, "pressed button emits maximum charge");
        Grid up;
        up.put(0, 0, 0, tile, 1);
        require(signalAt(0, 0, 0, up.reader()) == 0, "released button emits nothing");
        require(isPowerSource(tile), "button must be a charge source");
    }

    // A pressed button charges an adjacent wire.
    Grid grid;
    grid.put(0, 0, 0, TileButtonWood, 1 | 8);
    for (int x = 1; x <= 3; ++x) grid.put(x, 0, 0, TileRedstoneDust, 0);
    const auto changes = propagate(1, 0, 0, grid.reader());
    require(changeAt(changes, 1, 0, 0) == 15, "button must charge the wire beside it");
    require(changeAt(changes, 3, 0, 0) == 13, "charge decays away from the button");

    // The module is pure, so the button does NOT pop back up on its own: the
    // state is unchanged until the caller applies a release.
    const int held = buttonPress(1);
    require(buttonPressed(held), "pure module must not auto-release the button");
}

// ------------------------------------------------------------------ torch ---
void torchInversion()
{
    stage = "torch";
    // The defining behaviour: a charged input puts the torch OUT.
    require(torchTileForInput(TileRedstoneTorchOn, true) == TileRedstoneTorchOff,
            "charged input must extinguish the torch");
    require(torchTileForInput(TileRedstoneTorchOff, false) == TileRedstoneTorchOn,
            "uncharged input must light the torch");
    require(torchTileForInput(TileRedstoneTorchOff, true) == TileRedstoneTorchOff, "stays out while charged");
    require(torchTileForInput(TileRedstoneTorchOn, false) == TileRedstoneTorchOn, "stays lit while uncharged");
    require(torchTileForInput(TileLever, true) == TileLever, "non torch tiles pass through");
    require(torchLit(TileRedstoneTorchOn) && !torchLit(TileRedstoneTorchOff), "lit test");

    // A lit torch is a source; an extinguished one is not.
    Grid grid;
    grid.put(0, 0, 0, TileRedstoneTorchOn, 5);
    require(signalAt(0, 0, 0, grid.reader()) == 15, "lit torch emits maximum charge");
    grid.clear();
    grid.put(0, 0, 0, TileRedstoneTorchOff, 5);
    require(signalAt(0, 0, 0, grid.reader()) == 0, "extinguished torch emits nothing");

    // Floor-mounted torch (data 5) is supported from below, so it withholds
    // output upwards but charges its horizontal neighbours.
    Grid floor;
    floor.put(0, 0, 0, TileRedstoneTorchOn, 5);
    require(signalTo(0, 0, 0, 1 /*up*/, floor.reader()) == 0, "torch must not drive the face it leans from");
    require(signalTo(0, 0, 0, 4 /*west*/, floor.reader()) == 15, "torch drives its sides");
    require(signalTo(0, 0, 0, 0 /*down*/, floor.reader()) == 15, "torch drives the block below it");

    // Inversion end to end: a charged support block puts the torch out.
    Grid inverter;
    inverter.put(0, 0, 0, TileLever, 1 | 8);     // support, charged
    inverter.put(0, 1, 0, TileRedstoneTorchOn, 5); // torch standing on it
    require(torchInputPowered(0, 1, 0, inverter.reader()), "torch reads its support block");
    require(torchResolve(0, 1, 0, inverter.reader()) == TileRedstoneTorchOff,
            "torch above a charged block must go out");

    Grid quiet;
    quiet.put(0, 0, 0, TileLever, 1);              // support, not charged
    quiet.put(0, 1, 0, TileRedstoneTorchOff, 5);
    require(!torchInputPowered(0, 1, 0, quiet.reader()), "uncharged support reads as no input");
    require(torchResolve(0, 1, 0, quiet.reader()) == TileRedstoneTorchOn,
            "torch above an uncharged block must light");

    // A lit torch charges a wire run.
    Grid wire;
    wire.put(0, 0, 0, TileRedstoneTorchOn, 5);
    for (int x = 1; x <= 3; ++x) wire.put(x, 0, 0, TileRedstoneDust, 0);
    const auto changes = propagate(1, 0, 0, wire.reader());
    require(changeAt(changes, 1, 0, 0) == 15, "torch charges the wire beside it");
    require(changeAt(changes, 3, 0, 0) == 13, "charge decays away from the torch");
}

// ----------------------------------------------------------------- purity ---
void purity()
{
    stage = "purity";
    Grid grid;
    grid.put(0, 0, 0, TileLever, 1 | 8);
    for (int x = 1; x <= 5; ++x) grid.put(x, 0, 0, TileRedstoneDust, 0);
    const auto before = grid.cells;
    const auto first = propagate(1, 0, 0, grid.reader());
    const auto second = propagate(1, 0, 0, grid.reader());
    require(grid.cells == before, "propagate must not mutate the world");
    require(first.size() == second.size(), "propagate must be deterministic");
    for (size_t i = 0; i < first.size(); ++i)
        require(first[i].x == second[i].x && first[i].y == second[i].y &&
                first[i].z == second[i].z && first[i].renderIndex == second[i].renderIndex,
                "repeated propagate must return identical changes");
    // Already-correct dust reports no change, so a commit loop terminates.
    Grid settled;
    settled.put(0, 0, 0, TileLever, 1 | 8);
    settled.put(1, 0, 0, TileRedstoneDust, 15);
    settled.put(2, 0, 0, TileRedstoneDust, 14);
    require(propagate(1, 0, 0, settled.reader()).empty(), "settled wire must report no changes");
    // A missing reader must not crash.
    require(signalAt(0, 0, 0, nullptr) == 0, "null reader reads as air");
    require(propagate(0, 0, 0, nullptr).empty(), "null reader yields no changes");
    require(signalAt(0, 0, 0, grid.reader()) == 15, "lever still reads after propagation");
    // Air and unrelated blocks are not sources.
    require(!isPowerSource(0), "air is not a source");
    require(!isPowerSource(1), "stone is not a source");
}

} // namespace

int main()
{
    renderStates();
    wireDecay();
    wireBranching();
    repeaterDelay();
    repeaterOneWay();
    leverSwitch();
    buttonRelease();
    torchInversion();
    purity();
    std::printf("PASS: render states present (lever/button/repeater/dust/torch), wire decay and "
                "branching, repeater delay 2/4/6/8 and one-way conduction, lever toggle, "
                "button release timing (wood 30 / stone 20), torch inversion, purity\n");
    return 0;
}
