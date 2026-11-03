#include "BlockTicks.hpp"

#include "Redstone.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>

// independent implementation, behaviour verified against observed rules.
//
// Delays used below, each read from the upstream tile that owns it:
//   * button: 30 ticks when sensitive (wood), 20 otherwise (stone). Supplied
//     by mcredstone::buttonReleaseDelayTicks().
//   * repeater/diode: delay step 1-4 doubled, so 2/4/6/8 ticks. Supplied by
//     mcredstone::repeaterDelayTicks().
//   * redstone torch: 2 ticks.
// Crops carry no scheduled delay upstream; they advance on random ticks, which
// this module does not implement, so the caller picks their delay.
namespace mcticks {
namespace {

constexpr int TorchDelayTicks = 2;

// Metadata layout. The powered bit is shared by lever and button; cocoa packs
// its age above the two direction bits.
constexpr int PoweredBit = 8;
constexpr int WheatMaxAge = 7;

bool isButton(int tileId)
{
    return tileId == mcredstone::TileButtonStone || tileId == mcredstone::TileButtonWood;
}

bool isRepeater(int tileId)
{
    return tileId == mcredstone::TileRepeaterOff || tileId == mcredstone::TileRepeaterOn;
}

bool isTorch(int tileId)
{
    return tileId == mcredstone::TileRedstoneTorchOff || tileId == mcredstone::TileRedstoneTorchOn;
}

// Tile family used for queue identity. Upstream compares queued entries with
// Tile::isMatching, under which a diode matches the other diode and a redstone
// torch matches the other torch, so booking the off state when the on state is
// already queued does not add a second entry. Collapsing each pair to one
// representative reproduces that.
int family(int tileId)
{
    if (isRepeater(tileId)) return mcredstone::TileRepeaterOff;
    if (isTorch(tileId)) return mcredstone::TileRedstoneTorchOff;
    return tileId;
}

// Total order used for firing. dueTick first, then the coordinate, then the
// tile id, which makes the outcome a function of the dt sequence alone.
bool earlier(const Scheduled& a, const Scheduled& b)
{
    if (a.dueTick != b.dueTick) return a.dueTick < b.dueTick;
    if (a.x != b.x) return a.x < b.x;
    if (a.y != b.y) return a.y < b.y;
    if (a.z != b.z) return a.z < b.z;
    return family(a.tileId) < family(b.tileId);
}

bool sameCell(const Scheduled& a, int x, int y, int z, int tileId)
{
    return a.x == x && a.y == y && a.z == z && family(a.tileId) == family(tileId);
}

bool validCoord(int x, int y, int z)
{
    if (y < 0 || y >= BlockTicks::WorldHeight) return false;
    const int limit = BlockTicks::MaxHorizontal;
    return x >= -limit && x < limit && z >= -limit && z < limit;
}

// --- per-behaviour resolution --------------------------------------------
// Each returns the catalog id the cell should settle to, or 0 for "no change".
// A 0 from mcredstone::catalogFor() means the appearance is absent, and is
// passed straight through as "no change": never substitute a wrong look.

uint16_t resolveButton(const mcredstone::BlockState& state)
{
    // Only a pressed button springs back; an already-released one is settled.
    if ((state.data & PoweredBit) == 0) return 0;
    return mcredstone::catalogFor(state.tileId, state.data & 7);
}

uint16_t resolveRepeater(int x, int y, int z, const mcredstone::BlockState& state,
                         const BlockReader& blockAt)
{
    const int settled = mcredstone::repeaterResolve(x, y, z, blockAt);
    if (settled == state.tileId) return 0;
    return mcredstone::catalogFor(settled, state.data);
}

uint16_t resolveTorch(int x, int y, int z, const mcredstone::BlockState& state,
                      const BlockReader& blockAt)
{
    const int settled = mcredstone::torchResolve(x, y, z, blockAt);
    if (settled == state.tileId) return 0;
    return mcredstone::catalogFor(settled, state.data);
}

uint16_t resolveWheat(const mcredstone::BlockState& state)
{
    // Upstream steps metadata by one and stops at 7. All eight appearances are
    // in the catalog, so every step can be shown.
    if (state.data >= WheatMaxAge) return 0;
    return mcredstone::catalogFor(TileWheat, state.data + 1);
}

// --- JSON ----------------------------------------------------------------
// A hand-rolled reader for exactly the shape toJson() writes. Anything else is
// rejected; this is a save-format reader, not a general JSON parser.

void skipSpace(const std::string& s, size_t& i)
{
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
}

bool expect(const std::string& s, size_t& i, char c)
{
    skipSpace(s, i);
    if (i >= s.size() || s[i] != c) return false;
    ++i;
    return true;
}

bool expectKey(const std::string& s, size_t& i, const char* key)
{
    skipSpace(s, i);
    const std::string quoted = std::string("\"") + key + "\"";
    if (s.compare(i, quoted.size(), quoted) != 0) return false;
    i += quoted.size();
    return expect(s, i, ':');
}

// Signed integers only, with no exponent: every numeric field in this format is
// an integer except "remainder", which is read separately.
bool readInt(const std::string& s, size_t& i, long long& out)
{
    skipSpace(s, i);
    const size_t start = i;
    if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
    const size_t digits = i;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
    if (i == digits) return false;
    // Hand-rolled on purpose: std::stoll/strtoll resolve to __isoc23_strtoll,
    // a GLIBC 2.38 symbol the device's 2.36 runtime cannot load, which makes
    // the whole library fail to dlopen. Overflow is rejected, not wrapped.
    const bool negative = s[start] == '-';
    unsigned long long magnitude = 0;
    constexpr unsigned long long limit = 9223372036854775807ull;
    for (size_t at = digits; at < i; ++at) {
        const unsigned digit = unsigned(s[at] - '0');
        if (magnitude > (limit - digit) / 10) return false;
        magnitude = magnitude * 10 + digit;
    }
    if (negative && magnitude > limit) return false;
    out = negative ? -static_cast<long long>(magnitude) : static_cast<long long>(magnitude);
    return true;
}

bool readDouble(const std::string& s, size_t& i, double& out)
{
    skipSpace(s, i);
    const size_t start = i;
    if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
    while (i < s.size() && ((s[i] >= '0' && s[i] <= '9') || s[i] == '.' || s[i] == 'e' ||
                            s[i] == 'E' || s[i] == '-' || s[i] == '+')) {
        // Stop before a sign that belongs to the next token rather than an exponent.
        if ((s[i] == '-' || s[i] == '+') && i != start && s[i - 1] != 'e' && s[i - 1] != 'E') break;
        ++i;
    }
    if (i == start) return false;
    try {
        out = std::stod(s.substr(start, i - start));
    } catch (const std::exception&) {
        return false;
    }
    return std::isfinite(out);
}

} // namespace

bool driven(int tileId)
{
    return isButton(tileId) || isRepeater(tileId) || isTorch(tileId) || tileId == TileWheat;
}

int defaultDelayTicks(int tileId)
{
    if (isButton(tileId)) return mcredstone::buttonReleaseDelayTicks(tileId);
    if (isTorch(tileId)) return TorchDelayTicks;
    // A repeater's delay is in its metadata, which schedule() does not see, so
    // the caller reads mcredstone::repeaterDelayTicks() itself. Crops have no
    // scheduled delay upstream at all.
    return 0;
}

bool BlockTicks::schedule(int x, int y, int z, int tileId, int delayTicks)
{
    if (!driven(tileId)) return false;
    if (!validCoord(x, y, z)) return false;
    if (delayTicks < MinDelayTicks || delayTicks > MaxDelayTicks) return false;
    if (queue_.size() >= Capacity) return false;
    for (const auto& entry : queue_)
        if (sameCell(entry, x, y, z, tileId)) return false;

    queue_.push_back({x, y, z, tileId, tick_ + uint64_t(delayTicks)});
    return true;
}

std::vector<Change> BlockTicks::advance(float dt, int budget, const BlockReader& blockAt)
{
    if (!std::isfinite(dt) || dt < 0.0f) throw std::invalid_argument("invalid elapsed time");
    if (budget < 0) throw std::invalid_argument("invalid tick budget");
    if (!blockAt) throw std::invalid_argument("missing block reader");

    // Same accumulation and clamp as Scene's fixed step: dt is capped at 0.1 s
    // so a long stall cannot burst into hundreds of ticks, and the tolerance
    // absorbs the representational error in float 1/20, 1/60 and friends.
    remainder_ += double(std::min(dt, MaxFrameSeconds)) * TicksPerSecond;
    uint64_t elapsed = 0;
    while (remainder_ >= 1 - 1e-6) {
        remainder_ -= 1;
        if (remainder_ < 0) remainder_ = 0;
        ++elapsed;
    }
    tick_ += elapsed;

    std::vector<Change> changes;
    if (queue_.empty() || budget == 0) return changes;

    // Sorting the whole queue, rather than only the due part, keeps the
    // leftovers in firing order for the next call.
    std::sort(queue_.begin(), queue_.end(), earlier);

    size_t fired = 0;
    while (fired < queue_.size() && fired < size_t(budget) && queue_[fired].dueTick <= tick_) {
        const Scheduled& entry = queue_[fired];
        ++fired;

        const mcredstone::BlockState state = mcredstone::decode(blockAt(entry.x, entry.y, entry.z));
        // The cell may have been replaced since it was booked. Upstream drops
        // the entry in that case rather than acting on whatever is there now.
        if (family(state.tileId) != family(entry.tileId)) continue;

        uint16_t next = 0;
        if (isButton(state.tileId)) next = resolveButton(state);
        else if (isRepeater(state.tileId)) next = resolveRepeater(entry.x, entry.y, entry.z, state, blockAt);
        else if (isTorch(state.tileId)) next = resolveTorch(entry.x, entry.y, entry.z, state, blockAt);
        else if (state.tileId == TileWheat) next = resolveWheat(state);

        if (next) changes.push_back({entry.x, entry.y, entry.z, int(next)});
    }

    // Consume exactly what fired. Entries past the budget stay, already sorted.
    queue_.erase(queue_.begin(), queue_.begin() + std::ptrdiff_t(fired));
    return changes;
}

std::vector<Scheduled> BlockTicks::pendingList() const
{
    std::vector<Scheduled> sorted = queue_;
    std::sort(sorted.begin(), sorted.end(), earlier);
    return sorted;
}

void BlockTicks::clear()
{
    queue_.clear();
    tick_ = 0;
    remainder_ = 0.0;
}

std::string BlockTicks::toJson() const
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(17);
    out << "{\"version\":1,\"tick\":" << tick_ << ",\"remainder\":" << remainder_
        << ",\"ticksPerSecond\":" << TicksPerSecond << ",\"pending\":[";
    bool first = true;
    for (const auto& entry : pendingList()) {
        if (!first) out << ',';
        first = false;
        out << "{\"x\":" << entry.x << ",\"y\":" << entry.y << ",\"z\":" << entry.z
            << ",\"tileId\":" << entry.tileId << ",\"dueTick\":" << entry.dueTick << '}';
    }
    out << "]}";
    return out.str();
}

bool BlockTicks::fromJson(const std::string& json)
{
    size_t i = 0;
    if (!expect(json, i, '{')) return false;

    if (!expectKey(json, i, "version")) return false;
    long long version = 0;
    if (!readInt(json, i, version) || version != 1) return false;
    if (!expect(json, i, ',')) return false;

    if (!expectKey(json, i, "tick")) return false;
    long long tick = 0;
    if (!readInt(json, i, tick) || tick < 0) return false;
    if (!expect(json, i, ',')) return false;

    if (!expectKey(json, i, "remainder")) return false;
    double remainder = 0.0;
    if (!readDouble(json, i, remainder) || remainder < 0.0 || remainder >= 1.0) return false;
    if (!expect(json, i, ',')) return false;

    if (!expectKey(json, i, "ticksPerSecond")) return false;
    long long tps = 0;
    if (!readInt(json, i, tps) || tps != TicksPerSecond) return false;
    if (!expect(json, i, ',')) return false;

    if (!expectKey(json, i, "pending")) return false;
    if (!expect(json, i, '[')) return false;

    // Built aside and only installed once the whole document validates, so a
    // bad field late in the list cannot leave the object half-restored.
    std::vector<Scheduled> restored;
    skipSpace(json, i);
    if (i < json.size() && json[i] == ']') {
        ++i;
    } else {
        for (;;) {
            if (!expect(json, i, '{')) return false;
            Scheduled entry;
            long long value = 0;

            if (!expectKey(json, i, "x") || !readInt(json, i, value)) return false;
            entry.x = int(value);
            if (!expect(json, i, ',')) return false;
            if (!expectKey(json, i, "y") || !readInt(json, i, value)) return false;
            entry.y = int(value);
            if (!expect(json, i, ',')) return false;
            if (!expectKey(json, i, "z") || !readInt(json, i, value)) return false;
            entry.z = int(value);
            if (!expect(json, i, ',')) return false;
            if (!expectKey(json, i, "tileId") || !readInt(json, i, value)) return false;
            entry.tileId = int(value);
            if (!expect(json, i, ',')) return false;
            if (!expectKey(json, i, "dueTick") || !readInt(json, i, value) || value < 0) return false;
            entry.dueTick = uint64_t(value);
            if (!expect(json, i, '}')) return false;

            if (!driven(entry.tileId)) return false;
            if (!validCoord(entry.x, entry.y, entry.z)) return false;
            if (restored.size() >= Capacity) return false;
            for (const auto& seen : restored)
                if (sameCell(seen, entry.x, entry.y, entry.z, entry.tileId)) return false;
            restored.push_back(entry);

            skipSpace(json, i);
            if (i < json.size() && json[i] == ',') {
                ++i;
                continue;
            }
            if (!expect(json, i, ']')) return false;
            break;
        }
    }

    if (!expect(json, i, '}')) return false;
    skipSpace(json, i);
    if (i != json.size()) return false;

    std::sort(restored.begin(), restored.end(), earlier);
    queue_ = std::move(restored);
    tick_ = uint64_t(tick);
    remainder_ = remainder;
    return true;
}

} // namespace mcticks
