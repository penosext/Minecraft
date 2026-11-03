#include "BlockEntities.hpp"
#include "World.hpp"
#include "LegacyCatalog.hpp"
#include "ItemCatalog.hpp"
#include "ToolRules.hpp"
#include "Crafting.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace mccontainers {
namespace {
using mcinventory::Slot;
// Companion I/O deliberately follows Inventory's hardened independent-store
// protocol, without modifying or sharing its existing serialization format.
using Bytes = std::vector<unsigned char>;
constexpr unsigned char Magic[8] = {'M','C','C','O','N','T','N',0};
constexpr uint32_t Version = 1;
constexpr size_t MaxEntities = 4096;
constexpr size_t MaxFile = 4 * 1024 * 1024, MaxMetadata = 1024 * 1024;
std::atomic<uint64_t> counter {0};
[[noreturn]] void fail(const std::string& text) { throw std::runtime_error("Containers: " + text); }
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
const sourceitems::ItemSpec* itemSpec(int index) {
    return index >= 0 && index < sourceitems::itemCount() ? sourceitems::items() + index : nullptr;
}
int itemIndex(const std::string& key) {
    if (key.empty()) return -1;
    for (int i = 0; i < sourceitems::itemCount(); ++i) if (key == sourceitems::items()[i].key) return i;
    return -1;
}
int maxDamage(const sourceitems::ItemSpec* spec) {
    const auto* tool = spec ? mcgame::toolProperties(spec->itemId) : nullptr;
    return tool ? tool->maxDamage : 0;
}
int stackLimit(const sourceitems::ItemSpec& spec) {
    return maxDamage(&spec) > 0 ? 1 : spec.maxStack;
}
bool validDamage(const sourceitems::ItemSpec& spec, int damage) {
    return damage >= 0 && damage <= maxDamage(&spec);
}
void validate(const Slot& slot) {
    if (slot.key.empty()) {
        if (slot.count != 0 || slot.damage != 0) fail("empty key with nonzero count/damage");
        return;
    }
    const auto* spec = itemSpec(itemIndex(slot.key));
    if (slot.key.size() > 256 || !spec || spec->maxStack < 1 || spec->maxStack > Inventory::MaxStack ||
        slot.count < 1 || slot.count > stackLimit(*spec) || !validDamage(*spec, slot.damage))
        fail("unknown item identity or invalid item stack count/damage");
}
std::string quote(const std::string& text) {
    std::string result = "\"";
    for (unsigned char c : text) {
        if (c < 32) {
            constexpr char hex[] = "0123456789abcdef";
            result += "\\u00"; result += hex[c >> 4]; result += hex[c & 15];
        } else {
            if (c == '"' || c == '\\') result += '\\';
            result += char(c); // Generated item names are UTF-8; identities remain ASCII.
        }
    }
    return result + '"';
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
    if (readFile(dir, "containers.bin", MaxFile, true) != expected) fail("refusing to overwrite changed/unknown containers.bin");
    Bytes committed = bytes; // Allocate bookkeeping before committing.
    const std::string temp = ".containers.bin.tmp-" + std::to_string(::getpid()) + "-" + std::to_string(++counter);
    FD fd(::openat(dir, temp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
    if (fd.value < 0) systemFail("create temporary containers");
    try {
        size_t at = 0;
        while (at < bytes.size()) {
            const ssize_t count = ::write(fd.value, bytes.data() + at, bytes.size() - at);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) systemFail("write containers");
            at += size_t(count);
        }
        if (::fsync(fd.value)) systemFail("flush containers");
        const int raw = fd.value; fd.value = -1;
        if (::close(raw)) systemFail("close containers");
        revalidate();
        if (readFile(dir, "containers.bin", MaxFile, true) != expected) fail("containers.bin changed before commit");
        if (expected.empty()) {
            if (::linkat(dir, temp.c_str(), dir, "containers.bin", 0)) systemFail("commit new containers");
            expected.swap(committed);
            if (::unlinkat(dir, temp.c_str(), 0)) systemFail("remove temporary containers");
        } else {
            if (::renameat(dir, temp.c_str(), dir, "containers.bin")) systemFail("replace containers");
            expected.swap(committed);
        }
        if (::fsync(dir)) systemFail("flush world directory");
    } catch (...) { ::unlinkat(dir, temp.c_str(), 0); throw; }
}

} // namespace

namespace {
constexpr int TileChest = 54;
constexpr int TileWorkbench = 58;
constexpr int TileFurnace = 61;
constexpr int TileLitFurnace = 62;
constexpr int FurnaceInput = 0;
constexpr int FurnaceFuel = 1;
constexpr int FurnaceOutput = 2;
constexpr int FurnaceCookTicks = 200;

int tileAt(const World& world, int x, int y, int z) {
    if (x < 0 || x >= world.width() || y < 0 || y >= world.height() || z < 0 || z >= world.depth()) return 0;
    const uint16_t block = world.blockAt(x, y, z);
    if (!block || block > legacy::kBlockCount) return 0;
    return legacy::kBlockSpecs[block - 1].tileId;
}

int slotsForTile(int tile) {
    return tile == TileChest ? 27 : tile == TileWorkbench ? 9 : tile == TileFurnace ? 3 : 0;
}

bool supportedTile(int tile) {
    return tile == TileChest || tile == TileWorkbench || tile == TileFurnace || tile == TileLitFurnace;
}

uint32_t floatBits(float value) {
    uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "float layout");
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

float bitsFloat(uint32_t bits) {
    float value = 0;
    static_assert(sizeof(bits) == sizeof(value), "float layout");
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void appendSlot(Bytes& bytes, const Slot& slot) {
    validate(slot);
    if (slot.key.size() > 256) fail("container item key too long");
    put32(bytes, uint32_t(slot.key.size()));
    put32(bytes, uint32_t(slot.count));
    put32(bytes, uint32_t(slot.damage));
    bytes.insert(bytes.end(), slot.key.begin(), slot.key.end());
}

Slot readSlot(const Bytes& bytes, Reader& reader) {
    const uint32_t length = reader.u32();
    const uint32_t count = reader.u32();
    const uint32_t damage = reader.u32();
    if (length > 256 || count > Inventory::MaxStack || damage > uint32_t(INT32_MAX) ||
        reader.at + length > bytes.size() - 4) fail("invalid container slot");
    Slot slot {std::string(bytes.begin() + reader.at, bytes.begin() + reader.at + length), int(count), int(damage)};
    reader.at += length;
    validate(slot);
    return slot;
}

bool nonEmpty(const std::vector<Slot>& slots) {
    for (const auto& slot : slots) if (slot.count) return true;
    return false;
}

std::string jsonSlot(const Slot& slot) {
    std::ostringstream out;
    out << "{\"key\":" << quote(slot.key) << ",\"count\":" << slot.count
        << ",\"damage\":" << slot.damage << '}';
    return out.str();
}

struct SmeltResult { int inputId, inputAux, outputId, outputAux, count; };

bool smeltRecipe(const Slot& slot, SmeltResult& result) {
    if (!slot.count) return false;
    const int index = itemIndex(slot.key);
    const auto* spec = itemSpec(index);
    if (!spec) return false;
    // TU19 FurnaceRecipes: the common ore, stone, sand and food paths that are
    // represented by this port's source-item catalogue. Unsupported recipes are
    // rejected instead of being silently converted to a guessed output.
    switch (spec->itemId) {
    case 4: result = {spec->itemId, spec->aux, 1, 0, 1}; return true; // cobblestone -> stone
    case 12: result = {spec->itemId, spec->aux, 20, 0, 1}; return true; // sand -> glass
    case 14: result = {spec->itemId, spec->aux, 266, 0, 1}; return true; // gold ore
    case 15: result = {spec->itemId, spec->aux, 265, 0, 1}; return true; // iron ore
    case 16: result = {spec->itemId, spec->aux, 263, 0, 1}; return true; // coal ore
    default: return false;
    }
}

int fuelTicks(const Slot& slot) {
    if (!slot.count) return 0;
    const auto* spec = itemSpec(itemIndex(slot.key));
    if (!spec) return 0;
    switch (spec->itemId) {
    case 263: return 1600; // coal
    case 5: return 300;    // any wooden plank
    case 17: return 300;   // any wooden log
    case 6: return 100;    // sapling
    default: return 0;
    }
}
}

void Containers::requireWorld(const World& world) const {
    if (!bound_ || !world.loaded() || world.path() != path_ || world.seed() != seed_ ||
        world.width() != width_ || world.height() != height_ || world.depth() != depth_ ||
        world.currentDimension() != dimension_ || world.originalGeneration() != original_)
        fail("container/world mismatch or container store not open");
}

void Containers::open(const World& world) {
    if (!world.loaded()) fail("world is not loaded");
    Containers candidate;
    Directory dir = directory(world.path());
    candidate.path_ = dir.path;
    const size_t slash = candidate.path_.rfind('/');
    if (slash == std::string::npos || slash == 0) fail("world must be inside a save root");
    candidate.root_ = candidate.path_.substr(0, slash);
    candidate.rootDevice_ = dir.info.st_dev;
    candidate.rootInode_ = dir.info.st_ino;
    Directory parent = directory(candidate.root_);
    candidate.rootDevice_ = parent.info.st_dev;
    candidate.rootInode_ = parent.info.st_ino;
    candidate.worldDevice_ = dir.info.st_dev;
    candidate.worldInode_ = dir.info.st_ino;
    candidate.seed_ = world.seed(); candidate.width_ = world.width(); candidate.height_ = world.height();
    candidate.depth_ = world.depth(); candidate.dimension_ = world.currentDimension(); candidate.original_ = world.originalGeneration();
    candidate.source_ = readFile(dir.fd.value, "level.json", MaxMetadata);
    sourceIdentity(dir.fd.value, candidate.source_, world);
    candidate.sourceHash_ = hash(candidate.source_);
    candidate.persisted_ = readFile(dir.fd.value, "containers.bin", MaxFile, true);
    if (!candidate.persisted_.empty()) {
        const Bytes& bytes = candidate.persisted_;
        if (bytes.size() < 8 + 10 * 4 + 4 || std::memcmp(bytes.data(), Magic, 8)) fail("invalid containers magic/size");
        Reader reader {bytes, 8};
        if (reader.u32() != Version || reader.u32() != candidate.seed_ || reader.u32() != uint32_t(candidate.width_) ||
            reader.u32() != uint32_t(candidate.height_) || reader.u32() != uint32_t(candidate.depth_) ||
            reader.u32() != candidate.sourceHash_ || reader.u32() != uint32_t(candidate.dimension_))
            fail("container version/world identity mismatch");
        const uint32_t count = reader.u32();
        if (count > MaxEntities) fail("too many container entities");
        std::set<Position> positions;
        for (uint32_t i = 0; i < count; ++i) {
            Position position {int(reader.u32()), int(reader.u32()), int(reader.u32())};
            const int tile = int(reader.u32());
            if (position[0] < 0 || position[0] >= candidate.width_ || position[1] < 0 || position[1] >= candidate.height_ ||
                position[2] < 0 || position[2] >= candidate.depth_ || !supportedTile(tile) || !positions.insert(position).second)
                fail("invalid container position/tile");
            const int storedSlots = int(reader.u32());
            if (storedSlots != slotsForTile(tile == TileLitFurnace ? TileFurnace : tile)) fail("invalid container slot count");
            Entity entity; entity.tile = tile == TileLitFurnace ? TileFurnace : tile; entity.slots.resize(size_t(storedSlots));
            entity.burn = int(reader.u32()); entity.burnTotal = int(reader.u32()); entity.cook = int(reader.u32());
            entity.fraction = bitsFloat(reader.u32());
            if (entity.burn < 0 || entity.burn > 20000 || entity.burnTotal < 0 || entity.burnTotal > 20000 ||
                entity.cook < 0 || entity.cook > FurnaceCookTicks || !std::isfinite(entity.fraction) ||
                entity.fraction < 0 || entity.fraction >= 1) fail("invalid furnace state");
            for (auto& slot : entity.slots) slot = readSlot(bytes, reader);
            if (entity.tile == TileFurnace && entity.burn && !entity.burnTotal) fail("invalid burning furnace state");
            // A container is bound to a real block. Refuse stale records rather
            // than silently detaching inventory from a mined or replaced block.
            const int actual = tileAt(world, position[0], position[1], position[2]);
            if (actual != tile && !(tile == TileFurnace && actual == TileLitFurnace)) fail("container block mismatch");
            candidate.entities_.emplace(position, std::move(entity));
        }
        if (reader.at + 4 != bytes.size() || reader.u32() != hash(bytes.data(), bytes.size() - 4)) fail("containers checksum mismatch");
    }
    Directory finalParent = directory(candidate.root_); identity(finalParent.info, candidate.rootDevice_, candidate.rootInode_);
    Directory finalDir = directory(candidate.path_); identity(finalDir.info, candidate.worldDevice_, candidate.worldInode_);
    if (readFile(finalDir.fd.value, "level.json", MaxMetadata) != candidate.source_ ||
        readFile(finalDir.fd.value, "containers.bin", MaxFile, true) != candidate.persisted_)
        fail("world/containers changed while opening");
    candidate.bound_ = true;
    *this = std::move(candidate);
}

void Containers::save(const World& world) {
    requireWorld(world);
    if (!dirty_) return;
    Directory parent = directory(root_); identity(parent.info, rootDevice_, rootInode_);
    Directory dir = directory(path_); identity(dir.info, worldDevice_, worldInode_);
    if (readFile(dir.fd.value, "level.json", MaxMetadata) != source_) fail("world metadata changed");
    sourceIdentity(dir.fd.value, source_, world);
    Bytes bytes(std::begin(Magic), std::end(Magic));
    put32(bytes, Version); put32(bytes, seed_); put32(bytes, uint32_t(width_)); put32(bytes, uint32_t(height_));
    put32(bytes, uint32_t(depth_)); put32(bytes, sourceHash_); put32(bytes, uint32_t(dimension_)); put32(bytes, uint32_t(entities_.size()));
    for (const auto& record : entities_) {
        const auto& pos = record.first; const auto& entity = record.second;
        if (!supportedTile(entity.tile) || entity.slots.size() != size_t(slotsForTile(entity.tile))) fail("invalid live container");
        put32(bytes, uint32_t(pos[0])); put32(bytes, uint32_t(pos[1])); put32(bytes, uint32_t(pos[2])); put32(bytes, uint32_t(entity.tile));
        put32(bytes, uint32_t(entity.slots.size())); put32(bytes, uint32_t(entity.burn)); put32(bytes, uint32_t(entity.burnTotal));
        put32(bytes, uint32_t(entity.cook)); put32(bytes, floatBits(float(entity.fraction)));
        for (const auto& slot : entity.slots) appendSlot(bytes, slot);
    }
    put32(bytes, hash(bytes));
    if (bytes.size() > MaxFile) fail("containers exceed bounded file size");
    auto revalidate = [&]() {
        Directory p = directory(root_); identity(p.info, rootDevice_, rootInode_);
        Directory d = directory(path_); identity(d.info, worldDevice_, worldInode_);
        if (readFile(d.fd.value, "level.json", MaxMetadata) != source_) fail("world metadata changed before container commit");
        sourceIdentity(d.fd.value, source_, world);
    };
    atomicWrite(dir.fd.value, bytes, persisted_, revalidate);
    dirty_ = false;
}

Containers::Entity* Containers::active(World& world) {
    requireWorld(world);
    if (!panel_) return nullptr;
    const auto found = entities_.find(selected_);
    return found == entities_.end() ? nullptr : &found->second;
}

bool Containers::interact(World& world, int x, int y, int z) {
    requireWorld(world);
    if (x < 0 || x >= world.width() || y < 0 || y >= world.height() || z < 0 || z >= world.depth()) return false;
    const int actual = tileAt(world, x, y, z);
    if (!supportedTile(actual)) return false;
    const float dx = world.playerX() - (float(x) + .5f), dy = world.playerY() + 1.62f - (float(y) + .5f), dz = world.playerZ() - (float(z) + .5f);
    if (dx * dx + dy * dy + dz * dz > 5.25f * 5.25f) return false;
    selected_ = {x, y, z};
    cursor_ = selected_; cursorValid_ = true; panel_ = true;
    auto found = entities_.find(selected_);
    if (found == entities_.end()) {
        Entity entity; entity.tile = actual == TileLitFurnace ? TileFurnace : actual; entity.slots.resize(size_t(slotsForTile(entity.tile)));
        entities_.emplace(selected_, std::move(entity)); dirty_ = true;
    } else if (actual == TileLitFurnace) found->second.tile = TileFurnace;
    return true;
}

void Containers::close() { panel_ = false; cursorValid_ = false; }
bool Containers::opened() const { return panel_; }

std::string Containers::infoJson(const Inventory& inventory) const {
    std::ostringstream out; out.imbue(std::locale::classic());
    out << "{\"open\":" << (panel_ ? "true" : "false");
    if (!panel_) return out.str() + '}';
    const auto found = entities_.find(selected_);
    if (found == entities_.end()) return out.str() + ",\"error\":\"missing-container\"}";
    const Entity& entity = found->second;
    const char* type = entity.tile == TileChest ? "chest" : entity.tile == TileWorkbench ? "workbench" : "furnace";
    out << ",\"type\":\"" << type << "\",\"x\":" << selected_[0] << ",\"y\":" << selected_[1] << ",\"z\":" << selected_[2]
        << ",\"burn\":" << entity.burn << ",\"burnTotal\":" << entity.burnTotal << ",\"cook\":" << entity.cook
        << ",\"inventory\":" << (inventory.opened() ? inventory.infoJson() : "{}");
    out << ",\"slots\":[";
    for (size_t i = 0; i < entity.slots.size(); ++i) { if (i) out << ','; out << jsonSlot(entity.slots[i]); }
    out << "]}";
    return out.str();
}

bool Containers::transfer(World& world, Inventory& inventory, int inventorySlot, int containerSlot, int count, bool toContainer) {
    Entity* entity = active(world);
    if (!entity || inventorySlot < 0 || inventorySlot >= Inventory::MainSlots || containerSlot < 0 ||
        containerSlot >= int(entity->slots.size()) || count <= 0 || count > Inventory::MaxStack) return false;
    if (!inventory.belongsTo(world)) return false;
    if (!inventory.exchangeSlot(inventorySlot, entity->slots[size_t(containerSlot)], count, toContainer)) return false;
    dirty_ = true;
    return true;
}

bool Containers::craft(World& world, Inventory& inventory) {
    Entity* entity = active(world);
    if (!entity || entity->tile != TileWorkbench || entity->slots.size() != 9 || !inventory.belongsTo(world)) return false;
    std::vector<std::string> grid; grid.reserve(9);
    for (const auto& slot : entity->slots) grid.push_back(slot.count ? slot.key : std::string());
    const auto match = crafting::match(grid);
    if (!match.matched || match.count <= 0 || !inventory.canAddItem(match.productItemId, match.productAux, match.count)) return false;
    auto nextEntity = *entity;
    auto nextInventory = inventory;
    const auto recipe = crafting::describe(match.recipeIndex);
    for (int i = 0; i < 9; ++i) {
        if (recipe.width == 0 || recipe.height == 0) return false;
        const int rx = i % 3, ry = i / 3;
        const int offX = (3 - recipe.width) / 2, offY = (3 - recipe.height) / 2;
        const int relX = rx - offX, relY = ry - offY;
        bool required = false;
        if (relX >= 0 && relY >= 0 && relX < recipe.width && relY < recipe.height) {
            const auto& cell = recipe.cells[size_t(relX + relY * recipe.width)]; required = !cell.empty;
        }
        if (required) {
            if (!nextEntity.slots[size_t(i)].count) return false;
            --nextEntity.slots[size_t(i)].count;
            if (!nextEntity.slots[size_t(i)].count) nextEntity.slots[size_t(i)] = {};
        }
    }
    if (!nextInventory.addItem(match.productItemId, match.productAux, match.count)) return false;
    *entity = std::move(nextEntity); inventory = std::move(nextInventory); dirty_ = true; return true;
}

void Containers::update(World& world, float dt) {
    requireWorld(world);
    if (!std::isfinite(dt) || dt < 0) fail("invalid container dt");
    dt = std::min(dt, .1f);
    for (auto& record : entities_) {
        auto& entity = record.second;
        if (entity.tile != TileFurnace || (tileAt(world, record.first[0], record.first[1], record.first[2]) != TileFurnace &&
            tileAt(world, record.first[0], record.first[1], record.first[2]) != TileLitFurnace)) continue;
        entity.fraction += double(dt) / .05;
        int ticks = std::min(200, int(std::floor(entity.fraction)));
        entity.fraction -= ticks;
        for (int i = 0; i < ticks; ++i) {
            SmeltResult recipe {};
            const bool canCook = smeltRecipe(entity.slots[FurnaceInput], recipe);
            const auto* output = canCook ? itemSpec(itemIndex("item:" + std::to_string(recipe.outputId) + ":" + std::to_string(recipe.outputAux))) : nullptr;
            const bool outputRoom = canCook && output &&
                (!entity.slots[FurnaceOutput].count || (entity.slots[FurnaceOutput].key == output->key &&
                    entity.slots[FurnaceOutput].count + recipe.count <= output->maxStack));
            if (entity.burn <= 0 && canCook && outputRoom) {
                const int fuel = fuelTicks(entity.slots[FurnaceFuel]);
                if (fuel > 0) { --entity.slots[FurnaceFuel].count; if (!entity.slots[FurnaceFuel].count) entity.slots[FurnaceFuel] = {}; entity.burn = fuel; entity.burnTotal = fuel; }
            }
            if (entity.burn > 0) {
                --entity.burn;
                if (canCook && outputRoom && ++entity.cook >= FurnaceCookTicks) {
                    --entity.slots[FurnaceInput].count; if (!entity.slots[FurnaceInput].count) entity.slots[FurnaceInput] = {};
                    if (!entity.slots[FurnaceOutput].count) entity.slots[FurnaceOutput] = {output->key, recipe.count, 0};
                    else entity.slots[FurnaceOutput].count += recipe.count;
                    entity.cook = 0;
                }
            } else entity.cook = 0;
        }
    }
    dirty_ = dirty_ || !entities_.empty();
}

bool Containers::canBreak(int x, int y, int z) const {
    const auto found = entities_.find({x, y, z});
    if (found == entities_.end()) return true;
    return !nonEmpty(found->second.slots) && found->second.burn <= 0;
}

void Containers::removed(int x, int y, int z) {
    const auto found = entities_.find({x, y, z});
    if (found == entities_.end() || !canBreak(x, y, z)) return;
    if (panel_ && selected_ == found->first) close();
    entities_.erase(found); dirty_ = true;
}

void Containers::dropOnDeath(const World& world, Inventory& inventory) {
    requireWorld(world);
    // Death-pile entities are intentionally not synthesized at an arbitrary
    // player coordinate. Until the entity store is present, preserve inventory
    // exactly; the caller's KeepInventory rule decides whether to clear it.
    (void)inventory;
}

void Containers::collectNearby(const World& world, Inventory& inventory) {
    requireWorld(world); (void)inventory;
}

} // namespace mccontainers
