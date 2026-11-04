#include "BlockEntities.hpp"
#include "World.hpp"
#include "LegacyCatalog.hpp"
#include "ItemCatalog.hpp"
#include "ToolRules.hpp"
#include "Crafting.hpp"
#include "Smelting.hpp"

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
constexpr uint32_t Version = 2; // v2 = v1 + player 2x2 grid trailer; v1 still read.
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
constexpr int FurnaceInput = 0;  // FurnaceTileEntity SLOT_INPUT
constexpr int FurnaceFuel = 1;   // SLOT_FUEL
constexpr int FurnaceOutput = 2; // SLOT_RESULT
constexpr int FurnaceCookTicks = smelting::kBurnInterval;
constexpr int MaxBurn = smelting::kBurnInterval * 100; // Longest source fuel (lava bucket).

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
    const auto* spec = itemSpec(itemIndex(slot.key));
    std::ostringstream out; out.imbue(std::locale::classic());
    out << "{\"key\":" << quote(slot.key) << ",\"count\":" << slot.count
        << ",\"damage\":" << slot.damage << ",\"itemId\":" << (spec ? spec->itemId : -1)
        << ",\"aux\":" << (spec ? spec->aux : -1) << ",\"maxStack\":" << (spec ? spec->maxStack : 0)
        << ",\"name\":" << quote(spec ? spec->name : "") << '}';
    return out.str();
}

const sourceitems::ItemSpec* slotSpec(const Slot& slot) {
    return slot.count ? itemSpec(itemIndex(slot.key)) : nullptr;
}

// FurnaceRecipes::getResult restricted to items this port's catalog has.
const sourceitems::ItemSpec* smeltOutput(const Slot& input) {
    const auto* spec = slotSpec(input);
    if (!spec) return nullptr;
    const auto result = smelting::smeltResult(spec->itemId);
    return result.ok ? itemSpec(sourceitems::itemIndexForIdAux(result.itemId, result.aux)) : nullptr;
}

int fuelTicks(const Slot& slot) {
    const auto* spec = slotSpec(slot);
    return spec ? smelting::burnDuration(spec->itemId, spec->aux) : 0;
}

// FurnaceTileEntity::canBurn (FurnaceTileEntity.cpp:250-261): result slot empty,
// or the same item with room under both the container and item stack limits.
bool canBurn(const std::vector<Slot>& slots) {
    const auto* output = smeltOutput(slots[FurnaceInput]);
    if (!output) return false;
    const Slot& result = slots[FurnaceOutput];
    if (!result.count) return true;
    if (result.key != output->key) return false;
    return result.count < std::min(Inventory::MaxStack, stackLimit(*output));
}

// FurnaceTileEntity::burn (:263-273).
void burnOne(std::vector<Slot>& slots) {
    const auto* output = smeltOutput(slots[FurnaceInput]);
    if (!output || !canBurn(slots)) return;
    Slot& result = slots[FurnaceOutput];
    if (!result.count) result = {output->key, 1, 0};
    else ++result.count;
    if (!--slots[FurnaceInput].count) slots[FurnaceInput] = {};
}

// Grid -> crafting::Grid (3x3). The 2x2 inventory grid occupies the top-left
// 2x2 of the matcher's 3x3, so any recipe wider/taller than 2 needs a cell
// that is always empty and therefore never matches (InventoryMenu craftSlots
// is a 2x2 CraftingContainer).
crafting::Grid craftGrid(const std::vector<Slot>& slots) {
    crafting::Grid grid(crafting::kGridSize);
    if (slots.size() == 9) {
        for (int i = 0; i < 9; ++i) grid[size_t(i)] = slots[size_t(i)].count ? slots[size_t(i)].key : std::string();
    } else if (slots.size() == 4) {
        for (int i = 0; i < 4; ++i)
            grid[size_t((i % 2) + (i / 2) * 3)] = slots[size_t(i)].count ? slots[size_t(i)].key : std::string();
    }
    return grid;
}

// Largest prefix of `slot` that the inventory accepts, as moveItemStackTo does
// for a partial move. Returns 0 when nothing fits.
int acceptable(const Inventory& inventory, const Slot& slot) {
    const auto* spec = slotSpec(slot);
    if (!spec) return 0;
    for (int count = slot.count; count > 0; --count)
        if (inventory.canAddItem(spec->itemId, spec->aux, count, slot.damage)) return count;
    return 0;
}
} // namespace

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
        const uint32_t version = reader.u32();
        if ((version != 1 && version != Version) || reader.u32() != candidate.seed_ || reader.u32() != uint32_t(candidate.width_) ||
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
            if (entity.burn < 0 || entity.burn > MaxBurn || entity.burnTotal < 0 || entity.burnTotal > MaxBurn ||
                entity.cook < 0 || entity.cook > FurnaceCookTicks || !std::isfinite(entity.fraction) ||
                entity.fraction < 0 || entity.fraction >= 1) fail("invalid furnace state");
            for (auto& slot : entity.slots) slot = readSlot(bytes, reader);
            if (entity.tile == TileFurnace && entity.burn && !entity.burnTotal) fail("invalid burning furnace state");
            // A record whose resident cell now holds another block is kept, not
            // rejected: blocks can disappear through paths that never call
            // removed() (old builds did not guard mining at all), and failing
            // here would make the whole world unopenable. Its items stay safe and
            // reappear if the same container block is placed there again.
            candidate.entities_.emplace(position, std::move(entity));
        }
        if (version >= 2) {
            // v2 trailer: the persistent player 2x2 crafting grid.
            if (reader.u32() != 4) fail("invalid player grid size");
            for (auto& slot : candidate.playerGrid_) slot = readSlot(bytes, reader);
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
        put32(bytes, uint32_t(entity.cook)); put32(bytes, floatBits(std::min(float(entity.fraction), 0.99999994f)));
        for (const auto& slot : entity.slots) appendSlot(bytes, slot);
    }
    put32(bytes, uint32_t(playerGrid_.size()));
    for (const auto& slot : playerGrid_) appendSlot(bytes, slot);
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

std::vector<Slot>* Containers::activeSlots() {
    if (screen_ == Screen::None) return nullptr;
    if (screen_ == Screen::Inventory) return &playerGrid_;
    const auto found = entities_.find(selected_);
    return found == entities_.end() ? nullptr : &found->second.slots;
}
const std::vector<Slot>* Containers::activeSlots() const {
    return const_cast<Containers*>(this)->activeSlots();
}
const Containers::Entity* Containers::activeEntity() const {
    if (screen_ == Screen::None || screen_ == Screen::Inventory) return nullptr;
    const auto found = entities_.find(selected_);
    return found == entities_.end() ? nullptr : &found->second;
}

bool Containers::interact(World& world, int x, int y, int z) {
    requireWorld(world);
    if (screen_ != Screen::None) return false;
    if (x < 0 || x >= world.width() || y < 0 || y >= world.height() || z < 0 || z >= world.depth()) return false;
    if (!world.chunkLoaded(x, z)) return false;
    const int actual = tileAt(world, x, y, z);
    if (!supportedTile(actual)) return false;
    // AbstractContainerMenu stillValid: distanceToSqr(x+.5, y+.5, z+.5) <= 8*8.
    const float dx = world.playerX() - (float(x) + .5f), dy = world.playerY() - (float(y) + .5f), dz = world.playerZ() - (float(z) + .5f);
    if (dx * dx + dy * dy + dz * dz > 8.0f * 8.0f) return false;
    const int tile = actual == TileLitFurnace ? TileFurnace : actual;
    const Position position {x, y, z};
    auto found = entities_.find(position);
    if (found != entities_.end() && found->second.tile != tile) {
        // A leftover record from a different container type at this cell (see
        // open()). Never mix inventories: keep refusing until it is emptied.
        if (nonEmpty(found->second.slots) || found->second.burn > 0) return false;
        entities_.erase(found); found = entities_.end();
    }
    if (found == entities_.end()) {
        Entity entity; entity.tile = tile; entity.slots.resize(size_t(slotsForTile(tile)));
        entities_.emplace(position, std::move(entity)); dirty_ = true;
    }
    selected_ = position;
    screen_ = tile == TileChest ? Screen::Chest : tile == TileWorkbench ? Screen::Workbench : Screen::Furnace;
    return true;
}

bool Containers::openInventory(const World& world) {
    requireWorld(world);
    if (screen_ != Screen::None) return screen_ == Screen::Inventory;
    screen_ = Screen::Inventory;
    return true;
}

int Containers::close(const World& world, Inventory& inventory) {
    requireWorld(world);
    if (screen_ == Screen::None) return 0;
    int left = 0;
    if (screen_ == Screen::Inventory || screen_ == Screen::Workbench) {
        auto* slots = activeSlots();
        if (slots && inventory.opened() && inventory.belongsTo(world)) {
            // CraftingMenu::removed / InventoryMenu::removed: every grid stack
            // goes back to the player. Partial fits move what they can.
            for (auto& slot : *slots) {
                if (!slot.count) continue;
                const auto* spec = slotSpec(slot);
                const int moved = acceptable(inventory, slot);
                if (moved && spec && inventory.addItem(spec->itemId, spec->aux, moved, slot.damage)) {
                    slot.count -= moved; if (!slot.count) slot = {};
                    dirty_ = true;
                }
                left += slot.count;
            }
        } else if (slots) {
            for (const auto& slot : *slots) left += slot.count;
        }
        if (screen_ == Screen::Workbench) {
            // Workbenches have no tile entity upstream; drop the empty record so
            // containers.bin only keeps benches that really hold leftovers.
            const auto found = entities_.find(selected_);
            if (found != entities_.end() && !nonEmpty(found->second.slots)) { entities_.erase(found); dirty_ = true; }
        }
    }
    screen_ = Screen::None;
    return left;
}

std::string Containers::infoJson(const Inventory& inventory) const {
    std::ostringstream out; out.imbue(std::locale::classic());
    out << "{\"open\":" << (opened() ? "true" : "false");
    if (!opened()) return out.str() + '}';
    const auto* slots = activeSlots();
    if (!slots) return out.str() + ",\"error\":\"missing-container\"}";
    const char* type = screen_ == Screen::Inventory ? "inventory" : screen_ == Screen::Chest ? "chest" :
                       screen_ == Screen::Workbench ? "workbench" : "furnace";
    out << ",\"type\":\"" << type << '"';
    if (screen_ != Screen::Inventory)
        out << ",\"x\":" << selected_[0] << ",\"y\":" << selected_[1] << ",\"z\":" << selected_[2];
    if (const auto* entity = activeEntity(); entity && entity->tile == TileFurnace)
        out << ",\"burnRemaining\":" << entity->burn << ",\"burnTotal\":" << entity->burnTotal
            << ",\"cookTicks\":" << entity->cook << ",\"cookTotal\":" << FurnaceCookTicks;
    if (screen_ == Screen::Inventory || screen_ == Screen::Workbench) {
        const auto match = preview();
        if (match.matched) out << ",\"recipe\":{\"key\":" << quote(match.productKey) << ",\"count\":" << match.count
                               << ",\"name\":" << quote(match.productName) << '}';
        else out << ",\"recipe\":null";
    }
    out << ",\"inventory\":" << (inventory.opened() ? inventory.infoJson() : "{}");
    out << ",\"slots\":[";
    for (size_t i = 0; i < slots->size(); ++i) { if (i) out << ','; out << jsonSlot((*slots)[i]); }
    out << "]}";
    return out.str();
}

mcinventory::PanelView Containers::view() const {
    mcinventory::PanelView result;
    const auto* slots = activeSlots();
    if (!slots) return result;
    result.kind = screen_ == Screen::Inventory ? mcinventory::PanelKind::Inventory :
                  screen_ == Screen::Workbench ? mcinventory::PanelKind::Workbench :
                  screen_ == Screen::Chest ? mcinventory::PanelKind::Chest : mcinventory::PanelKind::Furnace;
    result.slots = *slots;
    if (screen_ == Screen::Inventory || screen_ == Screen::Workbench) {
        const auto match = preview();
        if (match.matched) result.result = {match.productKey, match.count, 0};
    }
    if (const auto* entity = activeEntity(); entity && entity->tile == TileFurnace) {
        // FurnaceTileEntity::getLitProgress / getBurnProgress (:174-185).
        const int duration = entity->burnTotal ? entity->burnTotal : FurnaceCookTicks;
        result.lit = entity->burn > 0;
        result.litProgress = std::clamp(entity->burn * 12 / duration, 0, 12);
        result.burnProgress = std::clamp(entity->cook * 24 / FurnaceCookTicks, 0, 24);
    }
    return result;
}

bool Containers::transfer(World& world, Inventory& inventory, int inventorySlot, int containerSlot, int count, bool toContainer) {
    requireWorld(world);
    auto* slots = activeSlots();
    if (!slots || inventorySlot < 0 || inventorySlot >= Inventory::MainSlots || containerSlot < 0 ||
        containerSlot >= int(slots->size()) || count <= 0 || count > Inventory::MaxStack) return false;
    if (!inventory.belongsTo(world)) return false;
    // FurnaceResultSlot::mayPlace is false (FurnaceResultSlot.cpp:18).
    if (toContainer && screen_ == Screen::Furnace && containerSlot == FurnaceOutput) return false;
    if (!inventory.exchangeSlot(inventorySlot, (*slots)[size_t(containerSlot)], count, toContainer)) return false;
    dirty_ = true;
    return true;
}

bool Containers::quickMove(World& world, Inventory& inventory, int containerSlot) {
    requireWorld(world);
    auto* slots = activeSlots();
    if (!slots || containerSlot < 0 || containerSlot >= int(slots->size()) || !inventory.belongsTo(world)) return false;
    Slot& slot = (*slots)[size_t(containerSlot)];
    const auto* spec = slotSpec(slot);
    const int moved = acceptable(inventory, slot);
    if (!spec || !moved || !inventory.addItem(spec->itemId, spec->aux, moved, slot.damage)) return false;
    slot.count -= moved; if (!slot.count) slot = {};
    dirty_ = true;
    return true;
}

crafting::Match Containers::preview() const {
    if (screen_ != Screen::Inventory && screen_ != Screen::Workbench) return {};
    const auto* slots = activeSlots();
    if (!slots) return {};
    return crafting::match(craftGrid(*slots));
}

Containers::CraftResult Containers::craft(World& world, Inventory& inventory) {
    requireWorld(world);
    auto* slots = activeSlots();
    if (!slots || (screen_ != Screen::Inventory && screen_ != Screen::Workbench) || !inventory.belongsTo(world))
        return CraftResult::NoPanel;
    const auto match = crafting::match(craftGrid(*slots));
    if (!match.matched || match.count <= 0) return CraftResult::NoRecipe;
    // The touch UI has no cursor stack, so the product goes straight into the
    // inventory, as the shift-click path does (CraftingMenu.cpp:88-95). If it
    // does not fit, nothing is consumed (no player->drop).
    if (!inventory.canAddItem(match.productItemId, match.productAux, match.count)) return CraftResult::InventoryFull;
    // ResultSlot::onTake (ResultSlot.cpp:60-66): one item from EVERY occupied
    // grid slot. A shaped match leaves no surplus slot, so this is exactly the
    // recipe's ingredients wherever the offset/mirror placed them. No catalog
    // item has a crafting-remaining item (bucket/bottle), so :67-90 is moot.
    auto next = *slots;
    for (auto& slot : next) if (slot.count && !--slot.count) slot = {};
    if (!inventory.addItem(match.productItemId, match.productAux, match.count)) return CraftResult::InventoryFull;
    *slots = std::move(next); dirty_ = true;
    return CraftResult::Ok;
}

void Containers::update(World& world, float dt) {
    requireWorld(world);
    if (!std::isfinite(dt) || dt < 0) fail("invalid container dt");
    dt = std::min(dt, .1f);
    for (auto& record : entities_) {
        auto& entity = record.second;
        if (entity.tile != TileFurnace) continue;
        const auto& pos = record.first;
        // Only furnaces whose chunk is resident tick (no offline catch-up).
        if (!world.chunkLoaded(pos[0], pos[2])) continue;
        const int actual = tileAt(world, pos[0], pos[1], pos[2]);
        if (actual != TileFurnace && actual != TileLitFurnace) continue;
        entity.fraction += double(dt) / .05;
        const int ticks = int(std::floor(entity.fraction));
        entity.fraction -= ticks;
        if (entity.fraction >= 1 || entity.fraction < 0) entity.fraction = 0;
        auto& slots = entity.slots;
        for (int i = 0; i < ticks; ++i) {
            // FurnaceTileEntity::tick (FurnaceTileEntity.cpp:188-247).
            if (!entity.burn && !entity.cook && !canBurn(slots)) break; // Idle: nothing can change.
            const int before[3] = {entity.burn, entity.burnTotal, entity.cook};
            if (entity.burn > 0) --entity.burn;
            if (entity.burn == 0 && canBurn(slots)) {
                entity.burnTotal = entity.burn = fuelTicks(slots[FurnaceFuel]);
                if (entity.burn > 0 && !--slots[FurnaceFuel].count) slots[FurnaceFuel] = {};
            }
            if (entity.burn > 0 && canBurn(slots)) {
                if (++entity.cook == FurnaceCookTicks) { entity.cook = 0; burnOne(slots); }
            } else entity.cook = 0;
            if (before[0] != entity.burn || before[1] != entity.burnTotal || before[2] != entity.cook) dirty_ = true;
        }
    }
}

bool Containers::canBreak(int x, int y, int z) const {
    if (screen_ == Screen::Workbench || screen_ == Screen::Chest || screen_ == Screen::Furnace)
        if (selected_ == Position {x, y, z}) return false; // Never break the open panel's block.
    const auto found = entities_.find({x, y, z});
    if (found == entities_.end()) return true;
    return !nonEmpty(found->second.slots) && found->second.burn <= 0;
}

void Containers::removed(int x, int y, int z) {
    const auto found = entities_.find({x, y, z});
    if (found == entities_.end() || !canBreak(x, y, z)) return;
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
