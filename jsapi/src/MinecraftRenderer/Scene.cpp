#include "Scene.hpp"
#include "World.hpp"
#include "WorldRules.hpp"
#include "LegacyCatalog.hpp"

// Reuse the SAME global original_core Random linked by terrain. No alternate
// PRNG or duplicate class definition; the application seed is its legal uint32
// subset, widened without changing bits to the original int64 constructor.
#include <cstdint>
using byte = unsigned char;
#include <Random.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <iomanip>
#include <limits>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

// These namespace-isolated shells supply only time and clear-weather state.
// Actual methods, table initialization and scale are extracted unchanged.
namespace mcscene_original {
using mcscene::MobKind;
constexpr float PI = 3.141592654f; // original Definitions.h, checked by extractor
class Level;
class Dimension { public: float getTimeOfDay(int64_t time, float a) const; };
class Mth {
public:
    static void init();
    static float sin(float i), cos(float i);
    static float* _sin;
    static const float sinScale;
};
class Level {
public:
    static const int TICKS_PER_DAY = 20 * 60 * 20; // original Level.h, checked
    explicit Level(uint64_t ticks) : time_(int64_t(ticks % TICKS_PER_DAY)) {}
    float getTimeOfDay(float a) { return dimension_.getTimeOfDay(time_, a); }
    float getRainLevel(float) { return 0; }
    float getThunderLevel(float) { return 0; }
    float getSkyDarken(float a);
    int getOldSkyDarken(float a);
    float getSunAngle(float a);
private:
    int64_t time_;
    Dimension dimension_;
};
using std::cos;
} // namespace mcscene_original
#include "LegacyScene.inc"

namespace mcscene {
namespace {
using mcworld::World;
using Bytes = std::vector<unsigned char>;
constexpr size_t Population = 8, V1RecordBytes = 40, V1HeaderBytes = 52;
// v2 retains the first 52 header bytes and first 40 bytes of each mob record.
// Header adds u64 kills + u32 population cooldown; records add health/maxHealth/
// hurtTime floats + u32 invulnerability ticks. v3 keeps that 64-byte header and
// appends a pre-armor lastHurt float to each 56-byte v2 record (now 60 bytes).
// All little-endian, FNV protected; old bytes remain pinned until explicit save.
constexpr size_t V2RecordBytes = 56, RecordBytes = 60, HeaderBytes = 64;
constexpr float Epsilon = .0001f, Pi = 3.141592654f;
constexpr unsigned char Magic[8] = {'M','C','S','C','E','N','E',0};
constexpr uint32_t Version = 3;
// Player.cpp:1597 supplies TOTAL attribute damage, not a weapon-only bonus.
// LivingEntity.cpp:785-798 compares raw lastHurt before actuallyHurt applies
// armor (1085-1094, 1129-1133). No crit/loot/full combat or original AI here.
// Platform input/file safety limit, not an upstream attack-attribute cap.
constexpr float FistDamage = 1, MaximumDamage = 1024;
constexpr uint32_t InvulnerableTicks = 20, HurtTicks = 10, PopulationCooldownTicks = 300;
float maximumHealth(MobKind kind) {
    switch (kind) {
    case MobKind::Pig: case MobKind::Cow: return 10;
    case MobKind::Sheep: return 8;
    case MobKind::Chicken: return 4;
    default: return 20; // Zombie inherits SharedMonsterAttributes default.
    }
}
std::atomic<uint64_t> tempCounter {0};
[[noreturn]] void fail(const std::string& message) { throw std::runtime_error("Scene: " + message); }
[[noreturn]] void systemFail(const std::string& message) { fail(message + ": " + std::strerror(errno)); }
struct FD {
    int value;
    explicit FD(int fd = -1) : value(fd) {}
    ~FD() { if (value >= 0) ::close(value); }
    FD(const FD&) = delete;
    FD& operator=(const FD&) = delete;
    FD(FD&& other) noexcept : value(other.value) { other.value = -1; }
    FD& operator=(FD&& other) noexcept {
        if (this != &other) { if (value >= 0) ::close(value); value = other.value; other.value = -1; }
        return *this;
    }
};
struct Directory { FD fd; std::string path; struct stat info {}; };
Directory directory(const std::string& path) {
    if (path.empty() || path[0] != '/' || path.size() > 4096 || path.find('\0') != std::string::npos)
        fail("expected bounded absolute world path");
    Directory result;
    result.fd = FD(::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (result.fd.value < 0) systemFail("open filesystem root");
    for (size_t begin = 1; begin < path.size();) {
        size_t end = path.find('/', begin);
        if (end == std::string::npos) end = path.size();
        std::string part = path.substr(begin, end - begin);
        if (part == "." || part == "..") fail("dot directory components forbidden");
        if (!part.empty()) {
            FD next(::openat(result.fd.value, part.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
            if (next.value < 0) systemFail("open directory " + part);
            result.fd = std::move(next);
            result.path += "/" + part;
        }
        begin = end + 1;
    }
    if (result.path.empty()) fail("filesystem root is not a save directory");
    if (::fstat(result.fd.value, &result.info) != 0) systemFail("stat directory");
    return result;
}
void identity(const struct stat& info, uint64_t device, uint64_t inode) {
    if (uint64_t(info.st_dev) != device || uint64_t(info.st_ino) != inode) fail("save directory was replaced");
}
Directory pinnedDirectory(const std::string& root, const std::string& name,
                          uint64_t rootDev, uint64_t rootIno, uint64_t worldDev, uint64_t worldIno) {
    Directory parent = directory(root);
    identity(parent.info, rootDev, rootIno);
    Directory result;
    result.fd = FD(::openat(parent.fd.value, name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    if (result.fd.value < 0) systemFail("open pinned world");
    if (::fstat(result.fd.value, &result.info) != 0) systemFail("stat pinned world");
    identity(result.info, worldDev, worldIno);
    result.path = root + "/" + name;
    return result;
}
Bytes readOptional(int dir) {
    FD fd(::openat(dir, "scene.bin", O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
    if (fd.value < 0) { if (errno == ENOENT) return {}; systemFail("open scene.bin"); }
    struct stat info {};
    if (::fstat(fd.value, &info) != 0) systemFail("stat scene.bin");
    if (!S_ISREG(info.st_mode) || info.st_nlink != 1 || info.st_size < 1 ||
        uint64_t(info.st_size) > HeaderBytes + Population * RecordBytes + 4) fail("invalid scene.bin file type/size/link count");
    Bytes bytes(size_t(info.st_size));
    size_t offset = 0;
    while (offset < bytes.size()) {
        ssize_t count = ::read(fd.value, bytes.data() + offset, bytes.size() - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) fail("truncated scene.bin");
        offset += size_t(count);
    }
    unsigned char extra;
    ssize_t count;
    do { count = ::read(fd.value, &extra, 1); } while (count < 0 && errno == EINTR);
    if (count != 0) fail("scene.bin changed while reading");
    struct stat after {}, named {};
    if (::fstat(fd.value, &after) || ::fstatat(dir, "scene.bin", &named, AT_SYMLINK_NOFOLLOW)) systemFail("restat scene.bin");
    if (after.st_size != info.st_size || named.st_dev != info.st_dev || named.st_ino != info.st_ino ||
        !S_ISREG(named.st_mode) || named.st_nlink != 1) fail("scene.bin replaced while reading");
    return bytes;
}
uint32_t hash(const unsigned char* bytes, size_t count) {
    uint32_t result = 2166136261u;
    for (size_t i = 0; i < count; ++i) result = (result ^ bytes[i]) * 16777619u;
    return result;
}
void put32(Bytes& bytes, uint32_t value) { for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<unsigned char>(value >> (8 * i))); }
void put64(Bytes& bytes, uint64_t value) { put32(bytes, uint32_t(value)); put32(bytes, uint32_t(value >> 32)); }
void putFloat(Bytes& bytes, float value) { uint32_t bits; std::memcpy(&bits, &value, 4); put32(bytes, bits); }
void putDouble(Bytes& bytes, double value) { uint64_t bits; std::memcpy(&bits, &value, 8); put64(bytes, bits); }
static_assert(sizeof(float) == 4 && sizeof(double) == 8 && std::numeric_limits<float>::is_iec559 &&
              std::numeric_limits<double>::is_iec559, "IEEE floats required");
struct Reader {
    const Bytes& bytes; size_t at = 0;
    uint32_t u32() {
        if (at + 4 > bytes.size()) fail("truncated scene state");
        uint32_t value = 0; for (int i = 0; i < 4; ++i) value |= uint32_t(bytes[at++]) << (8 * i);
        return value;
    }
    uint64_t u64() { uint64_t low = u32(); return low | (uint64_t(u32()) << 32); }
    float number() { uint32_t bits = u32(); float value; std::memcpy(&value, &bits, 4); if (!std::isfinite(value)) fail("nonfinite mob state"); return value; }
    double fraction() { uint64_t bits = u64(); double value; std::memcpy(&value, &bits, 8); if (!std::isfinite(value)) fail("nonfinite time remainder"); return value; }
};
void atomicWrite(int dir, const Bytes& bytes, const Bytes& expected) {
    if (readOptional(dir) != expected) fail("refusing to overwrite changed/unknown scene.bin");
    const std::string temp = ".scene.bin.tmp-" + std::to_string(::getpid()) + "-" + std::to_string(++tempCounter);
    FD fd(::openat(dir, temp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
    if (fd.value < 0) systemFail("create temporary scene");
    try {
        size_t offset = 0;
        while (offset < bytes.size()) {
            ssize_t count = ::write(fd.value, bytes.data() + offset, bytes.size() - offset);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) systemFail("write scene");
            offset += size_t(count);
        }
        if (::fsync(fd.value) != 0) systemFail("flush scene");
        int raw = fd.value; fd.value = -1;
        if (::close(raw) != 0) systemFail("close scene");
        if (readOptional(dir) != expected) fail("refusing to overwrite changed/unknown scene.bin");
        if (expected.empty()) {
            // Atomic no-replace creation, even if a target appears after check.
            if (::linkat(dir, temp.c_str(), dir, "scene.bin", 0) != 0) systemFail("commit new scene");
            if (::unlinkat(dir, temp.c_str(), 0) != 0) systemFail("unlink temporary scene");
        } else if (::renameat(dir, temp.c_str(), dir, "scene.bin") != 0) systemFail("replace scene");
        if (::fsync(dir) != 0) systemFail("flush world directory");
    } catch (...) { ::unlinkat(dir, temp.c_str(), 0); throw; }
}
// rules.json is a small independent text file in the same pinned world
// directory, with the same file-type/link/replacement safety as scene.bin. It
// is NOT part of the World's level.json, whose field count is validated by
// exact equality: adding fields there would make existing saves unopenable.
constexpr size_t MaxRulesBytes = 64 * 1024;
std::string readOptionalRules(int dir) {
    FD fd(::openat(dir, "rules.json", O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
    if (fd.value < 0) { if (errno == ENOENT) return {}; systemFail("open rules.json"); }
    struct stat info {};
    if (::fstat(fd.value, &info) != 0) systemFail("stat rules.json");
    if (!S_ISREG(info.st_mode) || info.st_nlink != 1 || info.st_size < 1 ||
        uint64_t(info.st_size) > MaxRulesBytes) fail("invalid rules.json file type/size/link count");
    std::string text(size_t(info.st_size), '\0');
    size_t offset = 0;
    while (offset < text.size()) {
        ssize_t count = ::read(fd.value, &text[offset], text.size() - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) fail("truncated rules.json");
        offset += size_t(count);
    }
    char extra;
    ssize_t count;
    do { count = ::read(fd.value, &extra, 1); } while (count < 0 && errno == EINTR);
    if (count != 0) fail("rules.json changed while reading");
    struct stat after {}, named {};
    if (::fstat(fd.value, &after) || ::fstatat(dir, "rules.json", &named, AT_SYMLINK_NOFOLLOW)) systemFail("restat rules.json");
    if (after.st_size != info.st_size || named.st_dev != info.st_dev || named.st_ino != info.st_ino ||
        !S_ISREG(named.st_mode) || named.st_nlink != 1) fail("rules.json replaced while reading");
    return text;
}
void atomicWriteRules(int dir, const std::string& text, const std::string& expected) {
    if (readOptionalRules(dir) != expected) fail("refusing to overwrite changed/unknown rules.json");
    const std::string temp = ".rules.json.tmp-" + std::to_string(::getpid()) + "-" + std::to_string(++tempCounter);
    FD fd(::openat(dir, temp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
    if (fd.value < 0) systemFail("create temporary rules");
    try {
        size_t offset = 0;
        while (offset < text.size()) {
            ssize_t count = ::write(fd.value, text.data() + offset, text.size() - offset);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) systemFail("write rules");
            offset += size_t(count);
        }
        if (::fsync(fd.value) != 0) systemFail("flush rules");
        int raw = fd.value; fd.value = -1;
        if (::close(raw) != 0) systemFail("close rules");
        if (readOptionalRules(dir) != expected) fail("refusing to overwrite changed/unknown rules.json");
        if (expected.empty()) {
            if (::linkat(dir, temp.c_str(), dir, "rules.json", 0) != 0) systemFail("commit new rules");
            if (::unlinkat(dir, temp.c_str(), 0) != 0) systemFail("unlink temporary rules");
        } else if (::renameat(dir, temp.c_str(), dir, "rules.json") != 0) systemFail("replace rules");
        if (::fsync(dir) != 0) systemFail("flush world directory");
    } catch (...) { ::unlinkat(dir, temp.c_str(), 0); throw; }
}
struct Body { float radius, height; };
Body body(MobKind kind) {
    switch (kind) {
    case MobKind::Chicken: return {.2f, .7f};
    case MobKind::Zombie: return {.3f, 1.8f};
    case MobKind::Cow: return {.45f, 1.4f};
    case MobKind::Sheep: return {.45f, 1.3f};
    default: return {.45f, .9f};
    }
}
bool residentBody(const World& world, const Mob& mob) {
    const float radius = body(mob.kind).radius - Epsilon;
    for (float dz : {-radius, radius}) for (float dx : {-radius, radius})
        if (!world.chunkLoaded(int(std::floor(mob.x + dx)), int(std::floor(mob.z + dz)))) return false;
    return true;
}
const legacy::BlockSpec* spec(const World& world, int x, int y, int z) {
    uint16_t id = world.blockAt(x, y, z);
    if (!id) return nullptr;
    if (id > legacy::kBlockCount) fail("world returned invalid catalog id");
    return &legacy::kBlockSpecs[id - 1];
}
bool obstacle(const World& world, int x, int y, int z) {
    if (x < 0 || x >= world.width() || y < 0 || y >= world.height() || z < 0 || z >= world.depth() || !world.chunkLoaded(x, z)) return true;
    const auto* tile = spec(world, x, y, z);
    // Conservatively full-cell collision, including liquids. Flora can be
    // crossed but is never accepted as a floor; no swimming/flying behavior.
    return tile && std::strcmp(tile->shape, "air") && std::strcmp(tile->shape, "cross") &&
           std::strcmp(tile->shape, "row") && std::strcmp(tile->shape, "lilypad");
}
bool floorTile(const World& world, int x, int y, int z) {
    if (y < 0 || !world.chunkLoaded(x, z)) return false;
    const auto* tile = spec(world, x, y, z);
    return tile && obstacle(world, x, y, z) && std::strcmp(tile->shape, "liquid") &&
           std::strcmp(tile->shape, "leaves") && std::strcmp(tile->key, "leaves") &&
           std::strncmp(tile->key, "leaves_", 7);
}
bool collides(const World& world, const Mob& mob, float x, float y, float z) {
    const Body b = body(mob.kind);
    if (x < b.radius || x > world.width() - b.radius || z < b.radius || z > world.depth() - b.radius ||
        y < 0 || y + b.height > world.height()) return true;
    for (int by = int(std::floor(y + Epsilon)); by <= int(std::floor(y + b.height - Epsilon)); ++by)
        for (int bz = int(std::floor(z - b.radius + Epsilon)); bz <= int(std::floor(z + b.radius - Epsilon)); ++bz)
            for (int bx = int(std::floor(x - b.radius + Epsilon)); bx <= int(std::floor(x + b.radius - Epsilon)); ++bx)
                if (obstacle(world, bx, by, bz)) return true;
    return false;
}
bool drySupport(const World& world, const Mob& mob, float x, float y, float z) {
    const float radius = body(mob.kind).radius - Epsilon;
    for (float dz : {-radius, radius}) for (float dx : {-radius, radius}) {
        bool found = false;
        for (int drop = 0; drop < 3; ++drop) {
            int by = int(std::floor(y - .02f)) - drop;
            const auto* tile = by >= 0 ? spec(world, int(std::floor(x + dx)), by, int(std::floor(z + dz))) : nullptr;
            if (tile && !std::strcmp(tile->shape, "liquid")) return false;
            if (floorTile(world, int(std::floor(x + dx)), by, int(std::floor(z + dz)))) { found = true; break; }
        }
        if (!found) return false;
    }
    return true;
}
bool aboveLiquid(const World& world, const Mob& mob) {
    // A removed floor may expose water after a mob was saved. Retire that
    // platform entity instead of letting the conservative obstacle surface turn
    // into water-walking or leaving it floating over a liquid cell.
    const float radius = body(mob.kind).radius - Epsilon;
    const int y = int(std::floor(mob.y - .02f));
    for (float dz : {-radius, radius}) for (float dx : {-radius, radius}) {
        const auto* tile = y >= 0 ? spec(world, int(std::floor(mob.x + dx)), y, int(std::floor(mob.z + dz))) : nullptr;
        if (tile && !std::strcmp(tile->shape, "liquid")) return true;
    }
    return false;
}
float distance2(const Mob& a, float x, float z) { float dx = a.x - x, dz = a.z - z; return dx * dx + dz * dz; }
bool validBounds(const World& world, const Mob& mob) {
    const Body b = body(mob.kind);
    return mob.x >= b.radius && mob.x <= world.width() - b.radius && mob.z >= b.radius &&
           mob.z <= world.depth() - b.radius && mob.y >= 0 && mob.y + b.height <= world.height();
}
// A new original Random is deterministically seeded for each fixed tick. No
// private Random state access and no home-grown recurrence; saves need only the
// tick plus each mob's brain state for exactly reproducible continuation.
int64_t tickSeed(uint32_t seed, uint64_t tick) {
    const uint64_t bits = uint64_t(seed) + tick * UINT64_C(341873128712);
    int64_t result; std::memcpy(&result, &bits, 8); return result;
}
const char* kindName(MobKind kind) {
    switch (kind) { case MobKind::Pig: return "Pig"; case MobKind::Cow: return "Cow";
    case MobKind::Sheep: return "Sheep"; case MobKind::Chicken: return "Chicken"; default: return "Zombie"; }
}
struct Target { size_t index; double distance; };
Target rayTarget(const World& world, const std::vector<Mob>& mobs, float yawDegrees, float pitchDegrees, float reach,
                 bool farPickRange = false) {
    if (!std::isfinite(yawDegrees) || !std::isfinite(pitchDegrees) || !std::isfinite(reach) || reach <= 0)
        fail("invalid combat ray input");
    constexpr double Radians = .017453292519943295769;
    const double yaw = std::remainder(double(yawDegrees), 360.0) * Radians;
    const double pitch = std::remainder(double(pitchDegrees), 360.0) * Radians;
    const std::array<double, 3> origin {{world.playerX(), double(world.playerY()) + 1.62, world.playerZ()}};
    std::array<double, 3> direction {{std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)}};
    for (double& d : direction) if (std::abs(d) < 1e-12) d = 0;
    const double limit = std::min(double(reach), farPickRange ? 6.0 : 3.0);
    Target result {mobs.size(), limit};
    // Segment/slab intersection is the nearest-face equivalent of original
    // AABB::clip, without linking the original Level/entity allocation graph.
    for (size_t i = 0; i < mobs.size(); ++i) {
        const Mob& mob = mobs[i]; const Body b = body(mob.kind);
        const double pickRadius = farPickRange ? 0.1 : 0.0;
        const std::array<double, 3> low {{double(mob.x) - b.radius - pickRadius, mob.y - pickRadius,
                                         double(mob.z) - b.radius - pickRadius}};
        const std::array<double, 3> high {{double(mob.x) + b.radius + pickRadius,
                                          double(mob.y) + b.height + pickRadius,
                                          double(mob.z) + b.radius + pickRadius}};
        double entry = 0, exit = limit;
        bool intersects = mob.health > 0;
        for (size_t axis = 0; axis < 3 && intersects; ++axis) {
            if (direction[axis] == 0) {
                if (origin[axis] < low[axis] || origin[axis] > high[axis]) intersects = false;
            } else {
                double a = (low[axis] - origin[axis]) / direction[axis];
                double z = (high[axis] - origin[axis]) / direction[axis];
                if (a > z) std::swap(a, z);
                entry = std::max(entry, a); exit = std::min(exit, z);
                if (entry > exit) intersects = false;
            }
        }
        if (intersects && (result.index == mobs.size() || entry < result.distance)) result = {i, entry};
    }
    if (result.index == mobs.size()) return result;
    // Exact voxel DDA to the entry face: no sampling gaps or through-wall hits.
    // Use World's mining-solid rule: even bedrock/foliage occludes, water does
    // not. World boundaries are barriers; edge-only touched cells are skipped.
    std::array<int, 3> voxel {}, step {};
    std::array<double, 3> next {}, delta {};
    for (size_t axis = 0; axis < 3; ++axis) {
        voxel[axis] = int(std::floor(origin[axis]));
        step[axis] = direction[axis] > 0 ? 1 : direction[axis] < 0 ? -1 : 0;
        if (!step[axis]) next[axis] = delta[axis] = std::numeric_limits<double>::infinity();
        else {
            delta[axis] = 1 / std::abs(direction[axis]);
            next[axis] = (voxel[axis] + (step[axis] > 0 ? 1 : 0) - origin[axis]) / direction[axis];
        }
    }
    for (;;) {
        const auto* tile = spec(world, voxel[0], voxel[1], voxel[2]);
        if (voxel[0] < 0 || voxel[0] >= world.width() || voxel[1] < 0 || voxel[1] >= world.height() ||
            voxel[2] < 0 || voxel[2] >= world.depth() || !world.chunkLoaded(voxel[0], voxel[2]) ||
            (tile && std::strcmp(tile->shape, "air") && std::strcmp(tile->shape, "liquid")))
            return {mobs.size(), limit};
        const double distance = std::min({next[0], next[1], next[2]});
        if (distance > result.distance + 1e-12) return result;
        for (size_t axis = 0; axis < 3; ++axis) if (next[axis] <= distance + 1e-12) {
            voxel[axis] += step[axis]; next[axis] += delta[axis];
        }
    }
}
void targetFields(std::ostream& out, const Mob& mob, double distance) {
    out << "\"id\":" << mob.id << ",\"kind\":\"" << kindName(mob.kind) << "\",\"health\":" << mob.health
        << ",\"maxHealth\":" << mob.maxHealth << ",\"distance\":" << distance;
}
} // namespace

void Scene::requireWorld(const World& world) const {
    if (!opened_ || !world.loaded() || world.path() != path_ || world.seed() != seed_ ||
        world.width() != width_ || world.height() != height_ || world.depth() != depth_) fail("scene/world mismatch or scene not open");
}
void Scene::open(const World& world) {
    if (!world.loaded()) fail("no world loaded");
    Scene candidate;
    Directory dir = directory(world.path());
    const size_t slash = dir.path.rfind('/');
    if (slash == 0 || slash == std::string::npos) fail("world must be inside a save root");
    candidate.path_ = dir.path;
    candidate.root_ = dir.path.substr(0, slash);
    candidate.name_ = dir.path.substr(slash + 1);
    Directory root = directory(candidate.root_);
    // Verify against the source World's ORIGINAL identities, not identities
    // discovered after an attacker has replaced a loaded world's directories.
    if (candidate.root_ != world.root_) fail("world path is outside its original save root");
    identity(root.info, world.rootDevice_, world.rootInode_);
    identity(dir.info, world.worldDevice_, world.worldInode_);
    candidate.rootDevice_ = world.rootDevice_; candidate.rootInode_ = world.rootInode_;
    candidate.worldDevice_ = world.worldDevice_; candidate.worldInode_ = world.worldInode_;
    // Re-open through the pinned root to close the root/world discovery gap.
    dir = pinnedDirectory(candidate.root_, candidate.name_, candidate.rootDevice_, candidate.rootInode_,
                          candidate.worldDevice_, candidate.worldInode_);
    candidate.seed_ = world.seed(); candidate.width_ = world.width(); candidate.height_ = world.height(); candidate.depth_ = world.depth();
    candidate.opened_ = true;
    // Rules first: population below must already honour DoMobSpawning.
    candidate.openRules(dir.fd.value);
    candidate.persisted_ = readOptional(dir.fd.value);
    if (candidate.persisted_.empty()) {
        candidate.populate(world, true);
    } else {
        const Bytes& bytes = candidate.persisted_;
        if (bytes.size() < V1HeaderBytes + 4 || std::memcmp(bytes.data(), Magic, 8)) fail("invalid scene magic/size");
        Reader r {bytes, 8};
        const uint32_t version = r.u32();
        if ((version != 1 && version != 2 && version != Version) || r.u32() != candidate.seed_ || r.u32() != uint32_t(candidate.width_) ||
            r.u32() != uint32_t(candidate.height_) || r.u32() != uint32_t(candidate.depth_)) fail("invalid scene version/world header");
        const uint32_t next = r.u32();
        if (next < 1 || next > uint32_t(INT32_MAX)) fail("invalid next mob id");
        candidate.nextId_ = int(next); candidate.ticks_ = r.u64(); candidate.remainder_ = r.fraction();
        if (candidate.remainder_ < 0 || candidate.remainder_ >= 1) fail("invalid tick remainder");
        const uint32_t count = r.u32();
        const size_t header = version == 1 ? V1HeaderBytes : HeaderBytes;
        const size_t record = version == 1 ? V1RecordBytes : version == 2 ? V2RecordBytes : RecordBytes;
        if (count > Population || bytes.size() != header + size_t(count) * record + 4) fail("invalid mob count/scene size");
        if (version >= 2) {
            candidate.killCount_ = r.u64(); candidate.populationCooldownTicks_ = r.u32();
            if (candidate.killCount_ >= next || candidate.populationCooldownTicks_ > PopulationCooldownTicks ||
                (candidate.populationCooldownTicks_ && !candidate.killCount_)) fail("invalid kill/population state");
        }
        std::set<int> ids;
        for (uint32_t i = 0; i < count; ++i) {
            Mob mob {}; Brain brain;
            const uint32_t id = r.u32(), kind = r.u32();
            if (!id || id >= next || kind > uint32_t(MobKind::Zombie)) fail("invalid mob id/kind");
            mob.id = int(id); mob.kind = MobKind(kind);
            mob.x = r.number(); mob.y = r.number(); mob.z = r.number(); mob.yaw = r.number(); mob.walkPhase = r.number();
            brain.velocityY = r.number(); brain.turnTicks = r.u32(); const uint32_t walking = r.u32();
            if (version == 1) mob.health = mob.maxHealth = maximumHealth(mob.kind);
            else {
                mob.health = r.number(); mob.maxHealth = r.number(); mob.hurtTime = r.number(); brain.invulnerableTime = r.u32();
                if (mob.maxHealth != maximumHealth(mob.kind) || mob.health <= 0 || mob.health > mob.maxHealth ||
                    mob.hurtTime < 0 || mob.hurtTime > HurtTicks || std::floor(mob.hurtTime) != mob.hurtTime ||
                    brain.invulnerableTime > InvulnerableTicks ||
                    mob.hurtTime != float(brain.invulnerableTime > HurtTicks ? brain.invulnerableTime - HurtTicks : 0))
                    fail("invalid mob health/hurt state");
                // v2 accepted only fists: infer raw 1, never reconstruct/reset HP
                // from armor-rounded health. Migration is strictly in memory.
                brain.lastHurt = version == 2 ? FistDamage : r.number();
                if (brain.lastHurt < 0 || brain.lastHurt > MaximumDamage ||
                    (brain.invulnerableTime && brain.lastHurt == 0)) fail("invalid last hurt damage");
            }
            if (!ids.insert(mob.id).second || !validBounds(world, mob) || mob.yaw < 0 || mob.yaw >= 360 ||
                mob.walkPhase < 0 || mob.walkPhase >= 2 * Pi || brain.velocityY < -32 || brain.velocityY > 0 ||
                brain.turnTicks > 200 || walking > 1) fail("invalid mob coordinates/brain state");
            // Terrain can legitimately change (mining), so lack of floor is
            // allowed and gravity will settle the mob; embedded bodies are not.
            // Distant saved mobs remain suspended until their chunks stream in.
            if (residentBody(world, mob) && collides(world, mob, mob.x, mob.y, mob.z)) fail("mob intersects terrain or liquid");
            brain.walking = walking != 0;
            candidate.mobs_.push_back(mob); candidate.brains_.push_back(brain);
        }
        if (r.u32() != hash(bytes.data(), bytes.size() - 4)) fail("scene checksum mismatch");
    }
    // The celestial freeze offset is only meaningful against the loaded tick
    // count, so it is validated here rather than when rules.json is parsed.
    if (candidate.frozenTicks_ > candidate.ticks_) fail("frozen day-cycle ticks exceed scene ticks");
    *this = std::move(candidate);
}

void Scene::openRules(int worldDirectory) {
    const std::string text = readOptionalRules(worldDirectory);
    // Old save with no rules.json: keep the checked defaults and write nothing
    // until an explicit save(), so merely opening an old world cannot alter it.
    if (text.empty()) return;
    if (!rules_.fromJson(text)) fail("invalid rules.json");
    uint64_t frozen = 0;
    if (rules_.extra("frozenDayTicks", frozen)) frozenTicks_ = frozen;
    persistedRules_ = text;
}
void Scene::saveRules(int worldDirectory) {
    mcworld::WorldRules stored = rules_;
    if (frozenTicks_) stored.setExtra("frozenDayTicks", frozenTicks_);
    else stored.clearExtra("frozenDayTicks");
    const std::string text = stored.toJson();
    // An untouched old save stays untouched: never create rules.json just to
    // record the defaults that a missing file already implies.
    if (persistedRules_.empty() && text == mcworld::WorldRules().toJson()) return;
    if (text == persistedRules_) return;
    atomicWriteRules(worldDirectory, text, persistedRules_);
    persistedRules_ = text;
}

bool Scene::setRule(mcworld::WorldRule rule, bool value) { return rules_.set(rule, value); }
bool Scene::applyRulesJson(const std::string& text) {
    mcworld::WorldRules candidate;
    if (!candidate.fromJson(text)) return false;
    // The freeze offset is scene state, not a caller-supplied rule: an
    // incoming document can never rewind or jump the celestial clock.
    rules_ = candidate;
    return true;
}
std::string Scene::rulesJson() const { return rules_.toJson(); }

// Mob eye (y + .85 * height, Mob::getHeadHeight default) to player eye (+1.62).
// Same occlusion rule as rayTarget: any non-air, non-liquid cell blocks sight.
static bool lineOfSight(const World& world, const Mob& mob) {
    const double ox = mob.x, oy = mob.y + body(mob.kind).height * .85, oz = mob.z;
    const double tx = world.playerX(), ty = world.playerY() + 1.62, tz = world.playerZ();
    const double length = std::sqrt((tx - ox) * (tx - ox) + (ty - oy) * (ty - oy) + (tz - oz) * (tz - oz));
    const int steps = std::max(1, int(std::ceil(length / .25)));
    for (int s = 1; s < steps; ++s) {
        const double t = double(s) / steps;
        const int x = int(std::floor(ox + (tx - ox) * t)), y = int(std::floor(oy + (ty - oy) * t)), z = int(std::floor(oz + (tz - oz) * t));
        if (x < 0 || y < 0 || z < 0 || x >= world.width() || y >= world.height() || z >= world.depth() || !world.chunkLoaded(x, z)) return false;
        const auto* tile = spec(world, x, y, z);
        if (tile && std::strcmp(tile->shape, "air") && std::strcmp(tile->shape, "liquid")) return false;
    }
    return true;
}

void Scene::save(const World& world) {
    requireWorld(world);
    Directory dir = pinnedDirectory(root_, name_, rootDevice_, rootInode_, worldDevice_, worldInode_);
    Bytes bytes(std::begin(Magic), std::end(Magic));
    put32(bytes, Version); put32(bytes, seed_); put32(bytes, uint32_t(width_)); put32(bytes, uint32_t(height_)); put32(bytes, uint32_t(depth_));
    put32(bytes, uint32_t(nextId_)); put64(bytes, ticks_); putDouble(bytes, remainder_); put32(bytes, uint32_t(mobs_.size()));
    put64(bytes, killCount_); put32(bytes, populationCooldownTicks_);
    for (size_t i = 0; i < mobs_.size(); ++i) {
        const Mob& m = mobs_[i]; const Brain& b = brains_[i];
        put32(bytes, uint32_t(m.id)); put32(bytes, uint32_t(m.kind));
        putFloat(bytes, m.x); putFloat(bytes, m.y); putFloat(bytes, m.z); putFloat(bytes, m.yaw); putFloat(bytes, m.walkPhase);
        putFloat(bytes, b.velocityY); put32(bytes, b.turnTicks); put32(bytes, b.walking ? 1 : 0);
        putFloat(bytes, m.health); putFloat(bytes, m.maxHealth); putFloat(bytes, m.hurtTime); put32(bytes, b.invulnerableTime);
        putFloat(bytes, b.lastHurt);
    }
    put32(bytes, hash(bytes.data(), bytes.size()));
    atomicWrite(dir.fd.value, bytes, persisted_);
    persisted_ = std::move(bytes);
    // Separate file, written after scene.bin. scene.bin remains byte-identical
    // to previous versions, so an older build still opens this world.
    saveRules(dir.fd.value);
}

std::string Scene::targetInfo(const World& world, float yaw, float pitch, bool farPickRange) const {
    requireWorld(world);
    const Target target = rayTarget(world, mobs_, yaw, pitch, farPickRange ? 6 : 3, farPickRange);
    if (target.index == mobs_.size()) return "{}";
    std::ostringstream out; out.imbue(std::locale::classic()); out << std::setprecision(8) << '{';
    targetFields(out, mobs_[target.index], target.distance); out << '}';
    return out.str();
}
std::string Scene::attack(const World& world, float yaw, float pitch, float reach, bool farPickRange, float baseDamage) {
    requireWorld(world);
    if (!std::isfinite(baseDamage) || baseDamage <= 0 || baseDamage > MaximumDamage) fail("invalid base damage");
    const Target target = rayTarget(world, mobs_, yaw, pitch, reach, farPickRange);
    const bool hit = target.index != mobs_.size();
    bool damaged = false, killed = false;
    uint32_t cooldown = 0;
    float damage = 0;
    Mob result {};
    if (hit) {
        Mob& mob = mobs_[target.index]; Brain& brain = brains_[target.index];
        const bool protectedHit = brain.invulnerableTime > InvulnerableTicks / 2;
        if (!protectedHit || baseDamage > brain.lastHurt) {
            const float rawDamage = protectedHit ? baseDamage - brain.lastHurt : baseDamage;
            // Only the accepted raw delta reaches actuallyHurt/armor. lastHurt
            // remembers the full RAW hit, never the delta or post-armor damage.
            damage = rawDamage * (25 - (mob.kind == MobKind::Zombie ? 2 : 0)) / 25;
            mob.health = std::max(0.0f, mob.health - damage);
            brain.lastHurt = baseDamage;
            if (!protectedHit) {
                mob.hurtTime = float(HurtTicks); brain.invulnerableTime = InvulnerableTicks;
            }
            damaged = true; killed = mob.health <= 0;
        }
        // Physical hit remains true even when lastHurt rejects damage; the
        // caller handles equipment wear for all hits, independently of damage.
        result = mob;
        cooldown = brain.invulnerableTime > InvulnerableTicks / 2 ? brain.invulnerableTime - InvulnerableTicks / 2 : 0;
        if (killed) {
            mobs_.erase(mobs_.begin() + target.index); brains_.erase(brains_.begin() + target.index);
            ++killCount_; populationCooldownTicks_ = PopulationCooldownTicks;
            cooldown = 0;
        }
    }
    std::ostringstream out; out.imbue(std::locale::classic()); out << std::setprecision(8);
    out << "{\"hit\":" << (hit ? "true" : "false") << ",\"damaged\":" << (damaged ? "true" : "false")
        << ",\"killed\":" << (killed ? "true" : "false") << ",\"damage\":" << damage
        << ",\"cooldownTicks\":" << cooldown << ",\"killCount\":" << killCount_;
    if (hit) { out << ','; targetFields(out, result, target.distance); }
    out << '}'; return out.str();
}

// Both read dayTicks(), the celestial clock, so a DoDaylightCycle freeze stops
// the sky and the light level together; the original functions are unchanged
// and still fed a plain tick count.
bool Scene::setDayTicks(uint64_t ticks) {
    if (!opened_) return false;
    // dayTicks() is ticks_ - frozenTicks_, so move the frozen offset rather
    // than the simulation clock: mob timers, hurt windows and AI keep running
    // while sky and light both follow the single new value.
    if (ticks > ticks_) return false; // Cannot run the celestial clock ahead of the simulation.
    frozenTicks_ = ticks_ - ticks;
    return true;
}
float Scene::timeOfDay() const {
    mcscene_original::Level level(dayTicks());
    return level.getTimeOfDay(float(remainder_));
}
float Scene::daylight() const {
    mcscene_original::Level level(dayTicks());
    return level.getSkyDarken(float(remainder_));
}

void Scene::populate(const World& world, bool initial) {
    // DoMobSpawning=false: spawn nothing at all, including the initial group
    // created for a brand new scene. Existing saved mobs are left alone.
    if (!rules_.get(mcworld::WorldRule::DoMobSpawning)) return;
    if (!initial && populationCooldownTicks_) return;
    Random random(tickSeed(seed_, ticks_));
    mcscene_original::Level level(dayTicks());
    const int rawLight = 15 - level.getOldSkyDarken(0);
    const bool night = rawLight <= 7;
    // Ensure all four requested passive species are visible in the starter
    // group. Additional passive choices use original Biome default weights;
    // this small near-player population is NOT upstream natural spawning.
    for (unsigned attempt = 0; mobs_.size() < Population && attempt < 384; ++attempt) {
        Mob mob {};
        bool have[4] = {};
        for (const auto& m : mobs_) if (m.kind != MobKind::Zombie) have[unsigned(m.kind)] = true;
        int missing = -1;
        for (int k = 0; k < 4; ++k) if (!have[k]) { missing = k; break; }
        if (night && !initial) {
            const size_t zombies = size_t(std::count_if(mobs_.begin(), mobs_.end(), [](const Mob& m) { return m.kind == MobKind::Zombie; }));
            if (zombies >= 2) break;
            mob.kind = MobKind::Zombie;
        } else {
            if (rawLight <= 8) break;
            if (missing >= 0) mob.kind = MobKind(missing);
            else {
                int total = 0; for (const auto& entry : mcscene_original::passiveWeights) total += entry.weight;
                int pick = random.nextInt(total);
                for (const auto& entry : mcscene_original::passiveWeights) { pick -= entry.weight; if (pick < 0) { mob.kind = entry.kind; break; } }
            }
        }
        const int range = initial ? 16 : 24;
        const int x = int(std::floor(world.playerX())) + random.nextInt(range * 2 + 1) - range;
        const int z = int(std::floor(world.playerZ())) + random.nextInt(range * 2 + 1) - range;
        mob.x = x + .5f; mob.z = z + .5f;
        const float dist = distance2(mob, world.playerX(), world.playerZ());
        if (dist < (night ? 8 * 8 : 4 * 4) || dist > range * range || x < 1 || z < 1 || x >= width_ - 1 || z >= depth_ - 1) continue;
        if (!world.chunkLoaded(x, z)) continue; // no remote blockAt-driven generation
        bool found = false;
        for (int y = height_ - 2; y > 0; --y) {
            const auto* tile = spec(world, x, y - 1, z);
            if (!tile) continue;
            if (!floorTile(world, x, y - 1, z)) break; // no water, foliage, canopy spawning
            if (mob.kind != MobKind::Zombie && std::strcmp(tile->key, "grass")) break;
            mob.y = float(y);
            if (std::abs(mob.y - world.playerY()) > 10 || collides(world, mob, mob.x, mob.y, mob.z) ||
                !drySupport(world, mob, mob.x, mob.y, mob.z)) break;
            found = true; break;
        }
        if (!found) continue;
        bool crowded = false;
        for (const Mob& m : mobs_) if (distance2(m, mob.x, mob.z) < 2.25f) { crowded = true; break; }
        if (crowded || nextId_ == INT32_MAX) continue;
        mob.id = nextId_++; mob.yaw = random.nextFloat() * 360;
        mob.health = mob.maxHealth = maximumHealth(mob.kind);
        mobs_.push_back(mob); brains_.push_back(Brain {0, uint32_t(20 + random.nextInt(60)), true});
    }
}

void Scene::tick(const World& world) {
    if (ticks_ == UINT64_MAX) fail("scene tick overflow");
    ++ticks_;
    // DoDaylightCycle=false advances the simulation but withholds this tick
    // from the celestial clock, so dayTicks() holds still. Mob AI, hurt timers
    // and cooldowns are unaffected; only sky time and light stop.
    if (!rules_.get(mcworld::WorldRule::DoDaylightCycle)) ++frozenTicks_;
    if (populationCooldownTicks_) --populationCooldownTicks_;
    Random random(tickSeed(seed_, ticks_));
    const bool day = daylight() > .55f;
    for (size_t i = 0; i < mobs_.size();) {
        Mob& mob = mobs_[i]; Brain& brain = brains_[i];
        if (!residentBody(world, mob)) { ++i; continue; }
        if (mob.hurtTime > 0) --mob.hurtTime;
        if (brain.invulnerableTime) --brain.invulnerableTime;
        // Keep a bounded near-player population; daylight removes the lightweight
        // night-only zombie representation (no fake claim of original burning).
        if (distance2(mob, world.playerX(), world.playerZ()) > 48 * 48 ||
            (mob.kind == MobKind::Zombie && day) || aboveLiquid(world, mob) || collides(world, mob, mob.x, mob.y, mob.z)) {
            mobs_.erase(mobs_.begin() + i); brains_.erase(brains_.begin() + i); continue;
        }
        if (!brain.turnTicks) {
            mob.yaw = random.nextFloat() * 360;
            brain.walking = random.nextInt(4) != 0;
            brain.turnTicks = uint32_t(30 + random.nextInt(90));
        } else --brain.turnTicks;
        if (mob.kind == MobKind::Zombie) {
            // MeleeAttackGoal.cpp:88-95: attackTime = max(attackTime-1, 0); hit
            // when distanceToSqr(target.x, target.bb.y0, target.z) <= (mob.bbWidth*2)^2
            // + target.bbWidth and attackTime == 0, then attackTime = 20.
            // Zombie.cpp:62 ATTACK_DAMAGE 3, FOLLOW_RANGE 40 (line 59).
            // Platform simplification: straight-line chase with an eye-to-eye
            // voxel sight test instead of PathNavigation; no reinforcements.
            if (brain.attackTime) --brain.attackTime;
            if (playerTargetable_) {
                const float dx = world.playerX() - mob.x, dz = world.playerZ() - mob.z;
                const float dy = world.playerY() - mob.y;
                const float horizontal = dx * dx + dz * dz;
                if (horizontal <= 40.0f * 40.0f && lineOfSight(world, mob)) {
                    mob.yaw = std::atan2(dx, dz) * 180 / Pi;
                    if (mob.yaw < 0) mob.yaw += 360;
                    constexpr float MobWidth = .6f, PlayerWidth = .6f; // Zombie / Player bbWidth.
                    const float melee = (MobWidth * 2) * (MobWidth * 2) + PlayerWidth;
                    const bool inReach = horizontal + dy * dy <= melee;
                    brain.walking = !inReach;
                    brain.turnTicks = std::max<uint32_t>(brain.turnTicks, 1);
                    if (inReach && !brain.attackTime) {
                        brain.attackTime = 20;
                        playerHits_.push_back({mob.id, 3.0f});
                    }
                }
            }
        }
        const float beforeX = mob.x, beforeZ = mob.z;
        if (brain.walking) {
            const float speed = mob.kind == MobKind::Chicken ? .9f : mob.kind == MobKind::Zombie ? .8f : .65f;
            const float radians = mob.yaw * Pi / 180;
            const float dx = std::sin(radians) * speed * .05f, dz = std::cos(radians) * speed * .05f;
            // Axis-separated sweep, small fixed steps, one-block autostep. A
            // dry floor within two blocks is required before approaching ledges.
            for (int axis = 0; axis < 2; ++axis) {
                const float nx = mob.x + (axis == 0 ? dx : 0), nz = mob.z + (axis == 1 ? dz : 0);
                if (!collides(world, mob, nx, mob.y, nz) && drySupport(world, mob, nx, mob.y, nz)) {
                    mob.x = nx; mob.z = nz;
                } else if (collides(world, mob, mob.x, mob.y - .02f, mob.z) &&
                           !collides(world, mob, mob.x, mob.y + 1, mob.z) &&
                           !collides(world, mob, nx, mob.y + 1, nz) &&
                           drySupport(world, mob, nx, mob.y + 1, nz)) {
                    // Never step onto a liquid surface, even though liquids are
                    // obstacles in this intentionally conservative physics.
                    if (floorTile(world, int(std::floor(nx)), int(std::floor(mob.y)), int(std::floor(nz)))) {
                        mob.x = nx; mob.z = nz; mob.y += 1; brain.velocityY = 0;
                    }
                }
            }
            const float moved = std::hypot(mob.x - beforeX, mob.z - beforeZ);
            // Fixed-step speed bounds this increment below one revolution;
            // subtraction avoids BOTH fmodf/fmod GLIBC_2.38 symbols on ARM64.
            if (moved > .00001f) {
                mob.walkPhase += moved * 5;
                if (mob.walkPhase >= 2 * Pi) mob.walkPhase -= 2 * Pi;
            }
            else brain.turnTicks = 0;
        }
        brain.velocityY = std::max(-32.0f, brain.velocityY - 20 * .05f);
        const float drop = brain.velocityY * .05f;
        // Vertical substeps prevent tunnelling at terminal velocity; bisection
        // lands the feet on the floor without introducing any hovering drift.
        const int steps = std::max(1, int(std::ceil(std::abs(drop) / .2f)));
        const float delta = drop / steps;
        for (int step = 0; step < steps; ++step) {
            if (!collides(world, mob, mob.x, mob.y + delta, mob.z)) mob.y += delta;
            else {
                float low = 0, high = 1;
                for (int n = 0; n < 16; ++n) {
                    const float mid = (low + high) * .5f;
                    if (collides(world, mob, mob.x, mob.y + delta * mid, mob.z)) high = mid; else low = mid;
                }
                mob.y += delta * low; brain.velocityY = 0; break;
            }
        }
        ++i;
    }
    // Reserve two slots for night creatures by retiring two passive platform
    // entries at night. Fixed ids remain unchanged for every surviving mob.
    if (ticks_ % 100 == 0) {
        mcscene_original::Level level(dayTicks());
        // Only make room when spawning can actually fill the slots: with
        // DoMobSpawning=false this would just delete mobs permanently.
        if (rules_.get(mcworld::WorldRule::DoMobSpawning) && 15 - level.getOldSkyDarken(0) <= 7) {
            size_t zombies = size_t(std::count_if(mobs_.begin(), mobs_.end(), [](const Mob& m) { return m.kind == MobKind::Zombie; }));
            while (mobs_.size() > Population - (2 - std::min(size_t(2), zombies))) {
                auto it = std::find_if(mobs_.rbegin(), mobs_.rend(), [](const Mob& m) { return m.kind != MobKind::Zombie; });
                if (it == mobs_.rend()) break;
                size_t at = size_t(std::distance(mobs_.begin(), it.base()) - 1);
                mobs_.erase(mobs_.begin() + at); brains_.erase(brains_.begin() + at);
            }
        }
        populate(world, false);
    }
}
void Scene::update(World& world, float dt) {
    requireWorld(world);
    if (!std::isfinite(dt) || dt < 0) fail("invalid elapsed time");
    remainder_ += double(std::min(dt, .1f)) * 20;
    // Tolerance absorbs representational error in float 1/20, 1/60 etc.; the
    // original tick duration is still exactly .05s, not frame-count based.
    while (remainder_ >= 1 - 1e-6) {
        remainder_ -= 1;
        if (remainder_ < 0) remainder_ = 0;
        tick(world);
    }
}
std::string Scene::summaryJson() const {
    std::ostringstream out; out.imbue(std::locale::classic()); out << std::setprecision(8);
    out << "{\"loaded\":" << (opened_ ? "true" : "false") << ",\"ticks\":" << ticks_
        << ",\"dayTicks\":" << dayTicks() << ",\"rules\":" << rules_.toJson()
        << ",\"ticksPerSecond\":20,\"ticksPerDay\":24000,\"timeOfDay\":" << timeOfDay()
        << ",\"phase\":" << timeOfDay() << ",\"daylight\":" << daylight() << ",\"ai\":\"lightweight-platform-wander-not-original-pathfinding\",\"mobCount\":"
        << mobs_.size() << ",\"killCount\":" << killCount_ << ",\"combat\":\"unarmed-subset-not-original-full-combat\",\"mobs\":[";
    bool first = true;
    for (const auto& mob : mobs_) {
        if (!first) out << ',';
        first = false;
        out << "{\"id\":" << mob.id << ",\"kind\":\"" << kindName(mob.kind) << "\",\"x\":" << mob.x
            << ",\"y\":" << mob.y << ",\"z\":" << mob.z << ",\"yaw\":" << mob.yaw << ",\"walkPhase\":" << mob.walkPhase
            << ",\"health\":" << mob.health << ",\"maxHealth\":" << mob.maxHealth << ",\"hurtTime\":" << mob.hurtTime << '}';
    }
    out << "]}";
    return out.str();
}
} // namespace mcscene
