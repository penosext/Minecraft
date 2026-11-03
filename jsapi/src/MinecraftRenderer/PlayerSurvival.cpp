#include "PlayerSurvival.hpp"
#include "World.hpp"
#include "LegacyCatalog.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <limits>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace mcplayer {
namespace {
using Bytes = std::vector<unsigned char>;
constexpr unsigned char Magic[8] = {'M','C','S','U','R','V','L',0};
constexpr uint32_t Version = 1;
constexpr size_t MaxFile = 112, MaxMetadata = 1024 * 1024;
std::atomic<uint64_t> counter {0};
[[noreturn]] void fail(const std::string& text) { throw std::runtime_error("Survival: " + text); }
[[noreturn]] void systemFail(const std::string& text) { fail(text + ": " + std::strerror(errno)); }
struct FD {
    int value = -1;
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
        const std::string part = path.substr(begin, end - begin);
        if (part == "." || part == "..") fail("dot path components forbidden");
        if (!part.empty()) {
            FD next(::openat(result.fd.value, part.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
            if (next.value < 0) systemFail("open directory " + part);
            result.fd = std::move(next);
            result.path += "/" + part;
        }
        begin = end + 1;
    }
    if (result.path.empty()) fail("filesystem root is not a world directory");
    if (::fstat(result.fd.value, &result.info)) systemFail("stat directory");
    return result;
}
void identity(const struct stat& info, uint64_t device, uint64_t inode) {
    if (uint64_t(info.st_dev) != device || uint64_t(info.st_ino) != inode) fail("save directory replaced");
}
Bytes readFile(int dir, const char* name, size_t maximum, bool optional = false) {
    FD fd(::openat(dir, name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
    if (fd.value < 0) {
        if (optional && errno == ENOENT) return {};
        systemFail(std::string("open ") + name);
    }
    struct stat before {};
    if (::fstat(fd.value, &before)) systemFail("stat file");
    if (!S_ISREG(before.st_mode) || before.st_nlink != 1 || before.st_size < 1 || uint64_t(before.st_size) > maximum)
        fail(std::string("invalid file type/size/link count: ") + name);
    Bytes bytes(size_t(before.st_size));
    size_t at = 0;
    while (at < bytes.size()) {
        const ssize_t count = ::read(fd.value, bytes.data() + at, bytes.size() - at);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) fail(std::string("truncated file: ") + name);
        at += size_t(count);
    }
    unsigned char extra;
    ssize_t count;
    do { count = ::read(fd.value, &extra, 1); } while (count < 0 && errno == EINTR);
    struct stat after {}, named {};
    if (count != 0 || ::fstat(fd.value, &after) || ::fstatat(dir, name, &named, AT_SYMLINK_NOFOLLOW))
        fail(std::string("file changed while reading: ") + name);
    if (!S_ISREG(named.st_mode) || named.st_nlink != 1 || after.st_nlink != 1 ||
        before.st_dev != named.st_dev || before.st_ino != named.st_ino || before.st_size != after.st_size ||
        before.st_mtim.tv_sec != after.st_mtim.tv_sec || before.st_mtim.tv_nsec != after.st_mtim.tv_nsec ||
        before.st_ctim.tv_sec != after.st_ctim.tv_sec || before.st_ctim.tv_nsec != after.st_ctim.tv_nsec)
        fail(std::string("file replaced/modified while reading: ") + name);
    return bytes;
}
uint32_t hash(const unsigned char* data, size_t count) {
    uint32_t result = 2166136261u;
    for (size_t i = 0; i < count; ++i) result = (result ^ data[i]) * 16777619u;
    return result;
}
uint32_t hash(const Bytes& bytes) { return hash(bytes.data(), bytes.size()); }
void put32(Bytes& bytes, uint32_t value) {
    for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<unsigned char>(value >> (8 * i)));
}
struct Reader {
    const Bytes& bytes; size_t at = 0;
    uint32_t u32() {
        if (at + 4 > bytes.size()) fail("truncated state");
        uint32_t value = 0;
        for (int i = 0; i < 4; ++i) value |= uint32_t(bytes[at++]) << (8 * i);
        return value;
    }
};
int catalogIndex(const std::string& key) {
    for (int i = 0; i < legacy::kBlockCount; ++i) if (key == legacy::kBlockSpecs[i].key) return i;
    return -1;
}
// Same deliberately restricted metadata grammar accepted by World: ASCII
// strings, unsigned integers, arrays and objects; duplicate keys are rejected.
// We need no World friendship and never write any World file.
struct Json {
    enum Kind { String, Number, Array, Object } kind = Number;
    std::string text;
    std::vector<Json> array;
    std::map<std::string, Json> object;
};
class Parser {
    const std::string& text; size_t at = 0;
    char take() { if (at == text.size()) fail("truncated metadata"); return text[at++]; }
    void space() { while (at < text.size() && std::strchr(" \t\r\n", text[at]) && text[at]) ++at; }
    std::string string() {
        if (take() != '"') fail("expected metadata string");
        std::string result;
        for (;;) {
            unsigned char c = static_cast<unsigned char>(take());
            if (c == '"') return result;
            if (c == '\\') {
                c = static_cast<unsigned char>(take());
                if (c == 'u') {
                    unsigned value = 0;
                    for (int i = 0; i < 4; ++i) {
                        char d = take(); int n = d >= '0' && d <= '9' ? d - '0' :
                            d >= 'a' && d <= 'f' ? d - 'a' + 10 : d >= 'A' && d <= 'F' ? d - 'A' + 10 : -1;
                        if (n < 0) fail("invalid metadata escape");
                        value = value * 16 + unsigned(n);
                    }
                    if (value < 32 || value >= 127) fail("non-ASCII metadata escape");
                    c = static_cast<unsigned char>(value);
                } else if (c != '"' && c != '\\' && c != '/') fail("invalid metadata escape");
            }
            if (c < 32 || c >= 127) fail("non-ASCII metadata string");
            result += char(c);
            if (result.size() > 256) fail("metadata string too long");
        }
    }
    Json read(int depth) {
        if (depth > 8) fail("metadata nesting too deep");
        space(); if (at == text.size()) fail("empty metadata value");
        Json result;
        if (text[at] == '"') { result.kind = Json::String; result.text = string(); return result; }
        if (text[at] == '[') {
            result.kind = Json::Array; ++at; space();
            if (at < text.size() && text[at] == ']') { ++at; return result; }
            for (;;) {
                result.array.push_back(read(depth + 1));
                if (result.array.size() > 65535) fail("oversized metadata array");
                space(); char c = take(); if (c == ']') return result;
                if (c != ',') fail("invalid metadata array");
            }
        }
        if (text[at] == '{') {
            result.kind = Json::Object; ++at; space();
            if (at < text.size() && text[at] == '}') { ++at; return result; }
            for (;;) {
                space(); std::string key = string(); space(); if (take() != ':') fail("invalid metadata object");
                if (!result.object.emplace(key, read(depth + 1)).second) fail("duplicate metadata key");
                if (result.object.size() > 32) fail("oversized metadata object");
                space(); char c = take(); if (c == '}') return result;
                if (c != ',') fail("invalid metadata object");
            }
        }
        size_t begin = at;
        while (at < text.size() && text[at] >= '0' && text[at] <= '9') ++at;
        if (at == begin || at - begin > 10 || (at - begin > 1 && text[begin] == '0')) fail("invalid metadata integer");
        result.text = text.substr(begin, at - begin); return result;
    }
public:
    explicit Parser(const std::string& input) : text(input) {}
    Json parse() { Json result = read(0); space(); if (at != text.size()) fail("trailing metadata"); return result; }
};
const Json& field(const Json& object, const char* key, Json::Kind kind) {
    const auto it = object.object.find(key);
    if (object.kind != Json::Object || it == object.object.end() || it->second.kind != kind) fail(std::string("invalid metadata ") + key);
    return it->second;
}
uint32_t number(const Json& value) {
    if (value.kind != Json::Number) fail("expected metadata integer");
    uint64_t result = 0; for (char c : value.text) result = result * 10 + unsigned(c - '0');
    if (result > UINT32_MAX) fail("metadata integer overflow");
    return uint32_t(result);
}
uint32_t number(const Json& object, const char* key) { return number(field(object, key, Json::Number)); }
void sourceIdentity(int dir, const Bytes& metadata, const mcworld::World& world) {
    const std::string text(metadata.begin(), metadata.end());
    const Json level = Parser(text).parse();
    if (number(level, "version") != 1 || number(level, "seed") != world.seed() ||
        field(level, "format", Json::String).text != (world.originalGeneration() ? "mcworld-original-terrain" : "mcworld-finite"))
        fail("world source version/seed/format mismatch");
    const auto& size = field(level, "size", Json::Array).array;
    if (size.size() != 3 || number(size[0]) != uint32_t(world.width()) ||
        number(size[1]) != uint32_t(world.height()) || number(size[2]) != uint32_t(world.depth())) fail("world dimensions mismatch");
    Bytes palette;
    std::set<std::string> unique;
    const auto& keys = field(level, "palette", Json::Array).array;
    if (keys.empty()) fail("empty world palette");
    for (const auto& value : keys) {
        if (value.kind != Json::String || catalogIndex(value.text) < 0 || !unique.insert(value.text).second)
            fail("unknown/duplicate world palette key");
        palette.insert(palette.end(), value.text.begin(), value.text.end()); palette.push_back(0);
    }
    const uint32_t ph = number(level, "paletteHash");
    if (hash(palette) != ph) fail("world palette hash mismatch");
    const uint32_t source = world.originalGeneration() ? hash(metadata) : number(level, "chunksHash");
    // A multi-dimension save appends the current dimension slot, so both the
    // original 44-byte record and the 48-byte one are valid. The trailing hash
    // still covers the whole record, so neither length can be forged as the other.
    const Bytes state = readFile(dir, "state.bin", 48);
    if ((state.size() != 44 && state.size() != 48) ||
        std::memcmp(state.data(), world.originalGeneration() ? "MCOSTAT\0" : "MCWSTAT\0", 8))
        fail("invalid world state identity");
    Reader r {state, 8};
    if (r.u32() != 1 || r.u32() != world.seed() || r.u32() != source || r.u32() != ph ||
        hash(state.data(), state.size() - 4) != Reader{state, state.size() - 4}.u32())
        fail("world state source hash mismatch");
    if (!world.originalGeneration()) {
        const Bytes chunks = readFile(dir, "chunks.bin", 40 + size_t(world.width()) * world.height() * world.depth() * 2);
        if (chunks.size() != 40 + size_t(world.width()) * world.height() * world.depth() * 2 ||
            std::memcmp(chunks.data(), "MCWCHNK\0", 8) || hash(chunks) != source) fail("world chunks identity mismatch");
    }
}
void atomicWrite(int dir, const Bytes& bytes, Bytes& expected, const std::function<void()>& revalidate) {
    if (readFile(dir, "survival.bin", MaxFile, true) != expected) fail("refusing to overwrite changed/unknown survival.bin");
    Bytes committed = bytes; // Allocate bookkeeping before committing.
    const std::string temp = ".survival.bin.tmp-" + std::to_string(::getpid()) + "-" + std::to_string(++counter);
    FD fd(::openat(dir, temp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
    if (fd.value < 0) systemFail("create temporary survival");
    try {
        size_t at = 0;
        while (at < bytes.size()) {
            const ssize_t count = ::write(fd.value, bytes.data() + at, bytes.size() - at);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) systemFail("write survival");
            at += size_t(count);
        }
        if (::fsync(fd.value)) systemFail("flush survival");
        const int raw = fd.value; fd.value = -1;
        if (::close(raw)) systemFail("close survival");
        revalidate();
        if (readFile(dir, "survival.bin", MaxFile, true) != expected) fail("survival.bin changed before commit");
        if (expected.empty()) {
            if (::linkat(dir, temp.c_str(), dir, "survival.bin", 0)) systemFail("commit new survival");
            expected.swap(committed);
            if (::unlinkat(dir, temp.c_str(), 0)) systemFail("remove temporary survival");
        } else {
            if (::renameat(dir, temp.c_str(), dir, "survival.bin")) systemFail("replace survival");
            expected.swap(committed);
        }
        if (::fsync(dir)) systemFail("flush world directory");
    } catch (...) { ::unlinkat(dir, temp.c_str(), 0); throw; }
}
uint32_t floatBits(float value) {
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559, "IEEE float required");
    uint32_t bits; std::memcpy(&bits, &value, 4); return bits;
}
float readFloat(Reader& reader, float low, float high) {
    const uint32_t bits = reader.u32(); float value; std::memcpy(&value, &bits, 4);
    if (!std::isfinite(value) || value < low || value > high) fail("invalid persisted float");
    return value;
}
int readInt(Reader& reader, int maximum) {
    const uint32_t value = reader.u32();
    if (value > uint32_t(maximum)) fail("invalid persisted integer");
    return int(value);
}
// Catalog IDs are not source Tile IDs. Resolve the source via the generated catalog.
int tile(const mcworld::World& world, int x, int y, int z) {
    const int id = world.blockAt(x, y, z);
    return id > 0 && id <= legacy::kBlockCount ? legacy::kBlockSpecs[id - 1].tileId : 0;
}
struct Environment { bool water = false, lava = false, fire = false, ladder = false; };
Environment environment(const mcworld::World& world) {
    Environment result;
    // World's player body is radius .25, height 1.8. Entity::move checks the
    // contracted body for contact fire; water contact extinguishes even when
    // the head is above water. No rain simulation or enchantments here.
    for (int x = int(std::floor(world.playerX() - .249f)); x <= int(std::floor(world.playerX() + .249f)); ++x)
        for (int z = int(std::floor(world.playerZ() - .249f)); z <= int(std::floor(world.playerZ() + .249f)); ++z)
            for (int y = int(std::floor(world.playerY() + .001f)); y <= int(std::floor(world.playerY() + 1.799f)); ++y) {
                const int id = tile(world, x, y, z);
                result.water |= id == 8 || id == 9;
                result.lava |= id == 10 || id == 11;
                result.fire |= id == 51;
                result.ladder |= id == 65 || id == 106;
            }
    return result;
}
} // namespace

void Survival::requireWorld(const mcworld::World& world) const {
    if (!opened_ || !world.loaded() || world.path() != path_ || world.seed() != seed_ ||
        world.width() != width_ || world.height() != height_ || world.depth() != depth_ ||
        world.originalGeneration() != original_) fail("survival is not open for this world");
}
void Survival::open(const mcworld::World& world) {
    if (!world.loaded()) fail("world is not loaded");
    Survival candidate;
    Directory dir = directory(world.path());
    candidate.path_ = dir.path;
    if (candidate.path_ != world.path()) fail("noncanonical world path");
    candidate.root_ = dir.path.substr(0, dir.path.rfind('/'));
    Directory parent = directory(candidate.root_);
    candidate.rootDevice_ = parent.info.st_dev; candidate.rootInode_ = parent.info.st_ino;
    candidate.worldDevice_ = dir.info.st_dev; candidate.worldInode_ = dir.info.st_ino;
    candidate.seed_ = world.seed(); candidate.width_ = world.width();
    candidate.height_ = world.height(); candidate.depth_ = world.depth();
    candidate.original_ = world.originalGeneration();
    candidate.source_ = readFile(dir.fd.value, "level.json", MaxMetadata);
    sourceIdentity(dir.fd.value, candidate.source_, world);
    candidate.sourceHash_ = hash(candidate.source_);
    candidate.persisted_ = readFile(dir.fd.value, "survival.bin", MaxFile, true);
    const auto& bytes = candidate.persisted_;
    if (!bytes.empty()) {
        // LE magic[8], version/seed/width/height/depth/levelHash, statistic
        // fields, IEEE double tick fraction, spawn xyz, and trailing FNV32.
        if (bytes.size() != MaxFile || std::memcmp(bytes.data(), Magic, 8)) fail("bad survival magic/size");
        Reader r {bytes, 8};
        if (r.u32() != Version || r.u32() != candidate.seed_ || r.u32() != uint32_t(candidate.width_) ||
            r.u32() != uint32_t(candidate.height_) || r.u32() != uint32_t(candidate.depth_) ||
            r.u32() != candidate.sourceHash_) fail("survival version/world identity mismatch");
        candidate.difficulty_ = readInt(r, 3);
        candidate.health_ = readFloat(r, 0, 20);
        candidate.food_ = readInt(r, 20);
        candidate.saturation_ = readFloat(r, 0, float(candidate.food_));
        candidate.exhaustion_ = readFloat(r, 0, 40);
        candidate.air_ = readInt(r, 320) - 20;
        if (candidate.air_ == -20) fail("unprocessed drowning state");
        candidate.foodTicks_ = readInt(r, 79);
        candidate.hurtTicks_ = readInt(r, 20);
        candidate.fireTicks_ = readInt(r, 321) - 20;
        candidate.peacefulTicks_ = readInt(r, 19);
        candidate.lastDamage_ = readFloat(r, 0, std::numeric_limits<float>::max());
        candidate.fallDistance_ = readFloat(r, 0, std::numeric_limits<float>::max());
        candidate.lastY_ = readFloat(r, 0, float(candidate.height_));
        candidate.positionKnown_ = readInt(r, 1) != 0;
        uint64_t remainder = r.u32(); remainder |= uint64_t(r.u32()) << 32;
        static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559, "IEEE double required");
        std::memcpy(&candidate.tickRemainder_, &remainder, 8);
        if (!std::isfinite(candidate.tickRemainder_) || candidate.tickRemainder_ < 0 || candidate.tickRemainder_ >= .05)
            fail("invalid tick remainder");
        candidate.spawnX_ = readFloat(r, 0, float(candidate.width_));
        candidate.spawnY_ = readFloat(r, 0, float(candidate.height_));
        candidate.spawnZ_ = readFloat(r, 0, float(candidate.depth_));
        if (r.at + 4 != bytes.size() || r.u32() != hash(bytes.data(), bytes.size() - 4)) fail("survival checksum mismatch");
    } else {
        candidate.spawnX_ = world.playerX();
        candidate.spawnY_ = world.playerY();
        candidate.spawnZ_ = world.playerZ();
    }
    Directory finalParent = directory(candidate.root_);
    identity(finalParent.info, candidate.rootDevice_, candidate.rootInode_);
    Directory finalDir = directory(candidate.path_);
    identity(finalDir.info, candidate.worldDevice_, candidate.worldInode_);
    if (readFile(finalDir.fd.value, "level.json", MaxMetadata) != candidate.source_ ||
        readFile(finalDir.fd.value, "survival.bin", MaxFile, true) != candidate.persisted_)
        fail("world/survival changed while opening");
    candidate.opened_ = true;
    *this = std::move(candidate);
}
void Survival::save(const mcworld::World& world) {
    requireWorld(world);
    auto revalidate = [&]() {
        Directory parent = directory(root_); identity(parent.info, rootDevice_, rootInode_);
        Directory dir = directory(path_); identity(dir.info, worldDevice_, worldInode_);
        if (readFile(dir.fd.value, "level.json", MaxMetadata) != source_) fail("world metadata changed");
        sourceIdentity(dir.fd.value, source_, world);
    };
    revalidate();
    Directory dir = directory(path_); identity(dir.info, worldDevice_, worldInode_);
    Bytes bytes(std::begin(Magic), std::end(Magic));
    for (uint32_t value : {Version, seed_, uint32_t(width_), uint32_t(height_), uint32_t(depth_), sourceHash_,
        uint32_t(difficulty_), floatBits(health_), uint32_t(food_), floatBits(saturation_), floatBits(exhaustion_),
        uint32_t(air_ + 20), uint32_t(foodTicks_), uint32_t(hurtTicks_), uint32_t(fireTicks_ + 20),
        uint32_t(peacefulTicks_), floatBits(lastDamage_), floatBits(fallDistance_), floatBits(lastY_), uint32_t(positionKnown_)})
        put32(bytes, value);
    uint64_t remainder; std::memcpy(&remainder, &tickRemainder_, 8);
    put32(bytes, uint32_t(remainder)); put32(bytes, uint32_t(remainder >> 32));
    put32(bytes, floatBits(spawnX_)); put32(bytes, floatBits(spawnY_)); put32(bytes, floatBits(spawnZ_));
    put32(bytes, hash(bytes));
    if (bytes.size() != MaxFile) fail("internal survival layout mismatch");
    atomicWrite(dir.fd.value, bytes, persisted_, revalidate);
}
bool Survival::setDifficulty(int difficulty) {
    if (difficulty < 0 || difficulty > 3) return false;
    difficulty_ = difficulty;
    return true;
}
bool Survival::hurt(float damage, bool creative) {
    if (!std::isfinite(damage) || damage < 0) fail("invalid damage");
    if (dead() || creative || creative_ || damage == 0) return false;
    // port/upstream/Minecraft.World/LivingEntity.cpp: invulnerableDuration=20.
    float applied = damage;
    if (hurtTicks_ > 10) {
        if (damage <= lastDamage_) return false;
        applied -= lastDamage_;
    } else hurtTicks_ = 20;
    lastDamage_ = damage;
    health_ = std::max(0.0f, health_ - applied);
    return true;
}
bool Survival::eat(int nutrition, float saturationModifier, bool creative) {
    if (nutrition < 0 || !std::isfinite(saturationModifier) || saturationModifier < 0) fail("invalid food");
    if (dead() || creative || creative_ || food_ >= 20 || nutrition == 0) return false;
    food_ += std::min(nutrition, 20 - food_);
    // FoodData::eat uses the full item nutrition, not just the missing food.
    saturation_ = float(std::min(double(food_), double(saturation_) + double(nutrition) * saturationModifier * 2));
    return true;
}
void Survival::exhaust(float amount) {
    if (!std::isfinite(amount) || amount < 0) fail("invalid exhaustion");
    if (!dead() && !creative_) exhaustion_ = float(std::min(40.0, double(exhaustion_) + amount));
}
void Survival::respawn() {
    health_ = 20; food_ = 20; saturation_ = 5; exhaustion_ = 0; air_ = 300;
    foodTicks_ = hurtTicks_ = fireTicks_ = peacefulTicks_ = 0;
    lastDamage_ = fallDistance_ = lastY_ = 0;
    tickRemainder_ = 0; positionKnown_ = false;
}
void Survival::update(mcworld::World& world, float dt, bool creative, bool naturalRegeneration) {
    requireWorld(world);
    if (!std::isfinite(dt) || dt < 0) fail("invalid dt");
    creative_ = creative;
    if (dead()) return;
    if (creative) { fallDistance_ = 0; fireTicks_ = 0; air_ = 300; }
    // Sample movement every frame, not once per tick: multiple physics frames
    // can occur before a tick, and a .1s update must not count a drop twice.
    const Environment env = environment(world);
    if (!creative && !env.water && !env.ladder && positionKnown_ && world.playerY() < lastY_)
        fallDistance_ = float(std::min(double(std::numeric_limits<float>::max()),
            double(fallDistance_) + lastY_ - world.playerY()));
    lastY_ = world.playerY(); positionKnown_ = true;
    if (env.water || env.ladder || creative) fallDistance_ = 0;
    if (world.grounded()) {
        if (fallDistance_ > 3) hurt(std::ceil(fallDistance_ - 3), creative);
        fallDistance_ = 0;
    }
    tickRemainder_ += std::min(double(dt), .1);
    // At most two ticks per call; fractional time is retained, never backlog.
    for (int i = 0; i < 2 && tickRemainder_ >= .05; ++i) {
        tickRemainder_ -= .05;
        if (!dead()) tick(world, creative, naturalRegeneration);
    }
}
void Survival::tick(mcworld::World& world, bool creative, bool naturalRegeneration) {
    if (hurtTicks_ > 0) --hurtTicks_;
    if (creative) return;
    const Environment env = environment(world);
    // Entity::baseTick / Entity::move: water extinguishes, burning hits every
    // 20 ticks; lava hits for 4 and refreshes 15 seconds. Player flameTime=20.
    if (env.water) fireTicks_ = 0;
    if (fireTicks_ > 0) {
        if (fireTicks_ % 20 == 0) hurt(1);
        --fireTicks_;
    }
    if (env.lava) { hurt(4); fireTicks_ = std::max(fireTicks_, 300); fallDistance_ *= .5f; }
    if (env.fire || env.lava) {
        hurt(1);
        if (!env.water && fireTicks_ < 301 && ++fireTicks_ == 0) fireTicks_ = 160;
    } else if (fireTicks_ <= 0) fireTicks_ = -20;
    if (env.water && fireTicks_ > 0) fireTicks_ = -20;
    if (dead()) return;
    // LivingEntity::baseTick: head, not feet, controls oxygen.
    if (world.underWater()) {
        if (--air_ == -20) { air_ = 0; hurt(2); }
    } else air_ = 300;
    if (dead()) return;
    // Player::aiStep's source expression is literally tickCount % 20 * 12,
    // hence every 20 ticks (not 240). Keep that port behavior.
    peacefulTicks_ = (peacefulTicks_ + 1) % 20;
    if (difficulty_ == 0 && naturalRegeneration && peacefulTicks_ == 0 && health_ < 20)
        health_ = std::min(20.0f, health_ + 1);
    // FoodData::tick: strict >4, at most one exhaustion conversion per tick.
    if (exhaustion_ > 4) {
        exhaustion_ -= 4;
        if (saturation_ > 0) saturation_ = std::max(0.0f, saturation_ - 1);
        else if (difficulty_ > 0) food_ = std::max(0, food_ - 1);
    }
    if (naturalRegeneration && food_ >= 18 && health_ < 20) {
        if (++foodTicks_ >= 80) { health_ = std::min(20.0f, health_ + 1); exhaust(3); foodTicks_ = 0; }
    } else if (food_ == 0) {
        if (++foodTicks_ >= 80) {
            if (health_ > 10 || difficulty_ >= 3 || (health_ > 1 && difficulty_ >= 2)) hurt(1);
            foodTicks_ = 0;
        }
    } else foodTicks_ = 0;
}
std::string Survival::infoJson() const {
    std::ostringstream out; out.imbue(std::locale::classic());
    out << "{\"health\":" << health_ << ",\"maxHealth\":20,\"food\":" << food_ << ",\"saturation\":" << saturation_
        << ",\"air\":" << air_ << ",\"maxAir\":300,\"exhaustion\":" << exhaustion_
        << ",\"dead\":" << (dead() ? "true" : "false")
        << ",\"difficulty\":" << difficulty_ << ",\"hurtTicks\":" << hurtTicks_ << ",\"fireTicks\":" << fireTicks_ << '}';
    return out.str();
}
} // namespace mcplayer
