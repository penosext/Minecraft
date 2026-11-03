#include "Redstone.hpp"

#include "LegacyCatalog.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <queue>
#include <tuple>

// independent implementation, rules verified against observed behaviour.
namespace mcredstone {
namespace {

// Facing codes, matching the catalog metadata encoding.
constexpr int Down = 0, Up = 1, North = 2, South = 3, West = 4, East = 5;
constexpr int FacingStepX[6] = {0, 0, 0, 0, -1, 1};
constexpr int FacingStepY[6] = {-1, 1, 0, 0, 0, 0};
constexpr int FacingStepZ[6] = {0, 0, -1, 1, 0, 0};
constexpr int OppositeFacing[6] = {Up, Down, South, North, East, West};

// Horizontal direction codes used by repeater metadata bits 0-1.
constexpr int DirSouth = 0, DirWest = 1, DirNorth = 2, DirEast = 3;
constexpr int DirStepX[4] = {0, -1, 0, 1};
constexpr int DirStepZ[4] = {1, 0, -1, 0};
// Direction -> the face a block in that direction is entered through.
constexpr int DirFacing[4] = {South, West, North, East};
constexpr int OppositeDirection[4] = {DirNorth, DirEast, DirSouth, DirWest};

constexpr int RepeaterDirectionMask = 0x3;
constexpr int RepeaterDelayMask = 0xC;
constexpr int RepeaterDelayShift = 2;
// Delay steps 1-4, each worth two game ticks.
constexpr int RepeaterDelaySteps[4] = {1, 2, 3, 4};
constexpr int RepeaterTicksPerStep = 2;

// Auto-release delays. Wood is the more sensitive tile and stays down longer.
constexpr int ButtonWoodReleaseTicks = 30;
constexpr int ButtonStoneReleaseTicks = 20;

constexpr int PoweredBit = 8;

bool isRepeater(int tileId)
{
    return tileId == TileRepeaterOff || tileId == TileRepeaterOn;
}
bool isButton(int tileId)
{
    return tileId == TileButtonStone || tileId == TileButtonWood;
}
bool isTorch(int tileId)
{
    return tileId == TileRedstoneTorchOff || tileId == TileRedstoneTorchOn;
}

// Catalog lookup tables. The catalog is a compile-time constant, so these are
// built once; nothing here depends on world state.
struct Tables {
    // Catalog id (index + 1) -> tile/metadata.
    std::vector<BlockState> byId;
    // tile * 16 + data -> catalog id, 0 when that appearance is absent.
    std::map<int, uint16_t> byState;
};

const Tables& tables()
{
    static const Tables built = [] {
        Tables result;
        result.byId.assign(size_t(legacy::kBlockCount) + 1, BlockState {-1, -1});
        result.byId[0] = {0, 0};
        for (int i = 0; i < legacy::kBlockCount; ++i) {
            const auto& spec = legacy::kBlockSpecs[i];
            result.byId[size_t(i) + 1] = {spec.tileId, spec.data};
            const int key = spec.tileId * 16 + spec.data;
            // Keep the first entry for a tile/metadata pair. Later duplicates
            // are older representative keys for the same appearance.
            result.byState.emplace(key, uint16_t(i + 1));
        }
        return result;
    }();
    return built;
}

uint16_t read(const BlockReader& blockAt, int x, int y, int z)
{
    return blockAt ? blockAt(x, y, z) : uint16_t(0);
}

BlockState stateAt(const BlockReader& blockAt, int x, int y, int z)
{
    return decode(read(blockAt, x, y, z));
}

// Charge a dust cell stores.
int dustLevel(const BlockState& state)
{
    return state.tileId == TileRedstoneDust ? std::clamp(state.data, SignalMin, SignalMax) : SignalNone;
}

// Which face a lever or torch is mounted against, as metadata 0-7 / 1-5.
// Lever: 0/7 ceiling, 5/6 floor, 1-4 walls. Torch: 5 floor, 1-4 walls.
int leverSupportFacing(int data)
{
    switch (data & 7) {
    case 1: return West;   // mounted on the -X wall, arm points +X
    case 2: return East;
    case 3: return North;
    case 4: return South;
    case 5: case 6: return Down;
    default: return Up;    // 0 and 7 hang from the ceiling
    }
}

} // namespace

BlockState decode(uint16_t block)
{
    const auto& table = tables().byId;
    if (block < table.size()) return table[block];
    return {-1, -1};
}

uint16_t catalogFor(int tileId, int data)
{
    if (tileId == 0) return 0;
    if (data < 0 || data > 15) return 0;
    const auto& map = tables().byState;
    const auto found = map.find(tileId * 16 + data);
    return found == map.end() ? uint16_t(0) : found->second;
}

bool isPowerSource(int tileId)
{
    return tileId == TileLever || isButton(tileId) || isRepeater(tileId) ||
           isTorch(tileId) || tileId == TileRedstoneDust;
}

int signalAt(int x, int y, int z, const BlockReader& blockAt)
{
    const BlockState state = stateAt(blockAt, x, y, z);
    switch (state.tileId) {
    case TileRedstoneDust:
        return dustLevel(state);
    case TileLever:
        return leverPowered(state.data) ? SignalMax : SignalNone;
    case TileButtonStone:
    case TileButtonWood:
        return buttonPressed(state.data) ? SignalMax : SignalNone;
    case TileRedstoneTorchOn:
        return SignalMax;
    case TileRedstoneTorchOff:
        return SignalNone;
    case TileRepeaterOn:
        return SignalMax;
    case TileRepeaterOff:
        return SignalNone;
    default:
        return SignalNone;
    }
}

int signalTo(int x, int y, int z, int facing, const BlockReader& blockAt)
{
    if (facing < 0 || facing > 5) return SignalNone;
    const BlockState state = stateAt(blockAt, x, y, z);
    // A repeater only drives the single cell in front of it, so asking from any
    // other side yields nothing. That is what makes conduction one-way.
    // Metadata bits 0-1 name the direction the INPUT sits in, so the output
    // leaves through the opposite face.
    if (isRepeater(state.tileId)) {
        if (state.tileId != TileRepeaterOn) return SignalNone;
        const int dir = state.data & RepeaterDirectionMask;
        return facing == DirFacing[OppositeDirection[dir]] ? SignalMax : SignalNone;
    }
    // A lit torch drives every face except the one it leans away from, which is
    // the face opposite its support block. A floor-mounted torch (data 5) is
    // supported from below and withholds its output upwards.
    if (state.tileId == TileRedstoneTorchOn) {
        const int support = leverSupportFacing(state.data);
        return facing == OppositeFacing[support] ? SignalNone : SignalMax;
    }
    return signalAt(x, y, z, blockAt);
}

int bestNeighbourSignal(int x, int y, int z, const BlockReader& blockAt)
{
    int best = SignalNone;
    for (int facing = 0; facing < 6; ++facing) {
        const int nx = x + FacingStepX[facing], ny = y + FacingStepY[facing], nz = z + FacingStepZ[facing];
        // The neighbour delivers charge into this cell through the opposite face.
        best = std::max(best, signalTo(nx, ny, nz, OppositeFacing[facing], blockAt));
    }
    return best;
}

std::vector<PowerSource> powerSources(int x, int y, int z, const BlockReader& blockAt)
{
    std::vector<PowerSource> result;
    for (int facing = 0; facing < 6; ++facing) {
        const int nx = x + FacingStepX[facing], ny = y + FacingStepY[facing], nz = z + FacingStepZ[facing];
        const BlockState state = stateAt(blockAt, nx, ny, nz);
        if (!isPowerSource(state.tileId)) continue;
        const int level = signalTo(nx, ny, nz, OppositeFacing[facing], blockAt);
        if (level > SignalNone) result.push_back({nx, ny, nz, level});
    }
    return result;
}

std::vector<Change> propagate(int x, int y, int z, const BlockReader& blockAt)
{
    using Key = std::tuple<int, int, int>;
    const BlockState origin = stateAt(blockAt, x, y, z);

    // Collect the dust network reachable from the origin. When the origin is
    // not dust itself (a lever or torch that just changed), start from the dust
    // cells around it.
    std::map<Key, int> network; // dust cell -> recomputed charge
    std::vector<Key> frontier;
    const auto note = [&](int cx, int cy, int cz) {
        if (stateAt(blockAt, cx, cy, cz).tileId != TileRedstoneDust) return;
        if (network.emplace(Key {cx, cy, cz}, SignalNone).second) frontier.push_back({cx, cy, cz});
    };
    if (origin.tileId == TileRedstoneDust) note(x, y, z);
    else
        for (int facing = 0; facing < 6; ++facing)
            note(x + FacingStepX[facing], y + FacingStepY[facing], z + FacingStepZ[facing]);

    // Flood the wire network. Dust connects horizontally and one step up/down,
    // which is what lets a line climb a block.
    for (size_t head = 0; head < frontier.size(); ++head) {
        const auto [cx, cy, cz] = frontier[head];
        for (int facing = 2; facing < 6; ++facing) {
            const int nx = cx + FacingStepX[facing], nz = cz + FacingStepZ[facing];
            for (int dy : {0, 1, -1}) note(nx, cy + dy, nz);
        }
    }

    // Seed every dust cell with the strongest charge injected by a
    // non-dust emitter next to it, then relax outwards. Using a max-first queue
    // means each cell is finalised once.
    using Entry = std::pair<int, Key>; // level, cell
    std::priority_queue<Entry> pending;
    for (auto& [cell, level] : network) {
        const auto [cx, cy, cz] = cell;
        int seed = SignalNone;
        for (int facing = 0; facing < 6; ++facing) {
            const int nx = cx + FacingStepX[facing], ny = cy + FacingStepY[facing], nz = cz + FacingStepZ[facing];
            if (stateAt(blockAt, nx, ny, nz).tileId == TileRedstoneDust) continue; // dust is relaxed, not a seed
            seed = std::max(seed, signalTo(nx, ny, nz, OppositeFacing[facing], blockAt));
        }
        if (seed > SignalNone) {
            level = seed;
            pending.push({seed, cell});
        }
    }
    while (!pending.empty()) {
        const auto [level, cell] = pending.top();
        pending.pop();
        const auto found = network.find(cell);
        if (found == network.end() || found->second > level) continue;
        // Charge drops by exactly one per cell and never below zero. A branch
        // therefore carries the same level as the cell it splits from.
        const int next = level - 1;
        if (next <= SignalNone) continue;
        const auto [cx, cy, cz] = cell;
        for (int facing = 2; facing < 6; ++facing) {
            const int nx = cx + FacingStepX[facing], nz = cz + FacingStepZ[facing];
            for (int dy : {0, 1, -1}) {
                const Key neighbour {nx, cy + dy, nz};
                const auto target = network.find(neighbour);
                if (target == network.end() || target->second >= next) continue;
                target->second = next;
                pending.push({next, neighbour});
            }
        }
    }

    // Emit only the cells whose stored charge actually moved, and only when the
    // new appearance exists in the catalog.
    std::vector<Change> changes;
    for (const auto& [cell, level] : network) {
        const auto [cx, cy, cz] = cell;
        const BlockState current = stateAt(blockAt, cx, cy, cz);
        const int clamped = std::clamp(level, SignalMin, SignalMax);
        if (current.data == clamped) continue;
        const uint16_t id = catalogFor(TileRedstoneDust, clamped);
        if (!id) continue; // never substitute a wrong appearance
        changes.push_back({cx, cy, cz, int(id)});
    }
    return changes;
}

int repeaterDelayStep(int data)
{
    return RepeaterDelaySteps[(data & RepeaterDelayMask) >> RepeaterDelayShift];
}

int repeaterDelayTicks(int data)
{
    return repeaterDelayStep(data) * RepeaterTicksPerStep;
}

int repeaterCycleDelay(int data)
{
    const int step = (data & RepeaterDelayMask) >> RepeaterDelayShift;
    const int next = ((step + 1) << RepeaterDelayShift) & RepeaterDelayMask;
    return next | (data & RepeaterDirectionMask);
}

int repeaterInputSignal(int x, int y, int z, const BlockReader& blockAt)
{
    const BlockState state = stateAt(blockAt, x, y, z);
    if (!isRepeater(state.tileId)) return SignalNone;
    // Metadata bits 0-1 point at the input side.
    const int dir = state.data & RepeaterDirectionMask;
    const int bx = x + DirStepX[dir], bz = z + DirStepZ[dir];
    const int behind = signalTo(bx, y, bz, DirFacing[OppositeDirection[dir]], blockAt);
    if (behind >= SignalMax) return behind;
    // Dust behind the repeater also feeds it at its stored level.
    return std::max(behind, dustLevel(stateAt(blockAt, bx, y, bz)));
}

int repeaterResolve(int x, int y, int z, const BlockReader& blockAt)
{
    const BlockState state = stateAt(blockAt, x, y, z);
    if (!isRepeater(state.tileId)) return state.tileId;
    return repeaterInputSignal(x, y, z, blockAt) > SignalNone ? TileRepeaterOn : TileRepeaterOff;
}

int buttonReleaseDelayTicks(int tileId)
{
    if (tileId == TileButtonWood) return ButtonWoodReleaseTicks;
    if (tileId == TileButtonStone) return ButtonStoneReleaseTicks;
    return 0;
}

bool leverPowered(int data) { return (data & PoweredBit) != 0; }

int leverToggle(int data)
{
    // Flip bit 8 and keep the mount orientation.
    return (data & 7) | (PoweredBit - (data & PoweredBit));
}

bool buttonPressed(int data) { return (data & PoweredBit) != 0; }

int buttonPress(int data)
{
    // Pressing an already-pressed button is a no-op; release is tick-driven.
    return (data & 7) | PoweredBit;
}

bool torchLit(int tileId) { return tileId == TileRedstoneTorchOn; }

int torchTileForInput(int tileId, bool inputPowered)
{
    if (!isTorch(tileId)) return tileId;
    // Inversion: a charged input extinguishes the torch.
    return inputPowered ? TileRedstoneTorchOff : TileRedstoneTorchOn;
}

bool torchInputPowered(int x, int y, int z, const BlockReader& blockAt)
{
    const BlockState state = stateAt(blockAt, x, y, z);
    if (!isTorch(state.tileId)) return false;
    // The torch reads only the block it is mounted on.
    const int support = leverSupportFacing(state.data);
    const int sx = x + FacingStepX[support], sy = y + FacingStepY[support], sz = z + FacingStepZ[support];
    return signalTo(sx, sy, sz, OppositeFacing[support], blockAt) > SignalNone;
}

int torchResolve(int x, int y, int z, const BlockReader& blockAt)
{
    const BlockState state = stateAt(blockAt, x, y, z);
    if (!isTorch(state.tileId)) return state.tileId;
    return torchTileForInput(state.tileId, torchInputPowered(x, y, z, blockAt));
}

} // namespace mcredstone
