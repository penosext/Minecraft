#include "Inventory.hpp"
#include "World.hpp"
#include "LegacyCatalog.hpp"
#include "ItemCatalog.hpp"
#include "ToolRules.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace mcinventory {
namespace {
using Bytes = std::vector<unsigned char>;
constexpr unsigned char Magic[8] = {'M','C','I','N','V','N','T',0};
constexpr uint32_t Version = 3;
constexpr size_t MaxLegacyUnresolved = Inventory::MainSlots + Inventory::ArmorSlots;
constexpr size_t MaxFile = 16384, MaxMetadata = 1024 * 1024;
std::atomic<uint64_t> counter {0};
[[noreturn]] void fail(const std::string& text) { throw std::runtime_error("Inventory: " + text); }
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
bool mergeable(const Slot& a, const Slot& b) {
    // ItemInstance::isStackable excludes damaged items, even with equal wear.
    return a.key == b.key && a.damage == 0 && b.damage == 0;
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
void validateLegacy(const Slot& slot) {
    if (slot.key.empty()) { if (slot.count != 0) fail("empty legacy key with nonzero count"); return; }
    if (slot.key.size() > 256 || catalogIndex(slot.key) < 0 || slot.count < 1 || slot.count > Inventory::MaxStack)
        fail("unknown legacy catalog key or invalid stack count");
}
void validateUnresolved(const LegacyUnresolvedSlot& slot) {
    if (slot.slot < 0 || slot.slot >= Inventory::MainSlots + Inventory::ArmorSlots || slot.key.empty())
        fail("invalid protected legacy slot");
    validateLegacy({slot.key, slot.count});
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
    if (readFile(dir, "inventory.bin", MaxFile, true) != expected) fail("refusing to overwrite changed/unknown inventory.bin");
    Bytes committed = bytes; // Allocate bookkeeping before committing.
    const std::string temp = ".inventory.bin.tmp-" + std::to_string(::getpid()) + "-" + std::to_string(++counter);
    FD fd(::openat(dir, temp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
    if (fd.value < 0) systemFail("create temporary inventory");
    try {
        size_t at = 0;
        while (at < bytes.size()) {
            const ssize_t count = ::write(fd.value, bytes.data() + at, bytes.size() - at);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) systemFail("write inventory");
            at += size_t(count);
        }
        if (::fsync(fd.value)) systemFail("flush inventory");
        const int raw = fd.value; fd.value = -1;
        if (::close(raw)) systemFail("close inventory");
        revalidate();
        if (readFile(dir, "inventory.bin", MaxFile, true) != expected) fail("inventory.bin changed before commit");
        if (expected.empty()) {
            if (::linkat(dir, temp.c_str(), dir, "inventory.bin", 0)) systemFail("commit new inventory");
            expected.swap(committed);
            if (::unlinkat(dir, temp.c_str(), 0)) systemFail("remove temporary inventory");
        } else {
            if (::renameat(dir, temp.c_str(), dir, "inventory.bin")) systemFail("replace inventory");
            expected.swap(committed);
        }
        if (::fsync(dir)) systemFail("flush world directory");
    } catch (...) { ::unlinkat(dir, temp.c_str(), 0); throw; }
}
} // namespace

void Inventory::requireOpened() const { if (!opened_) fail("inventory is not open"); }
void Inventory::requireWorld(const mcworld::World& world) const {
    requireOpened();
    if (!world.loaded() || world.path() != path_ || world.seed() != seed_ || world.width() != width_ ||
        world.height() != height_ || world.depth() != depth_ || world.originalGeneration() != original_)
        fail("inventory belongs to another world");
}
void Inventory::open(const mcworld::World& world) {
    if (!world.loaded()) fail("world is not loaded");
    Inventory candidate;
    Directory dir = directory(world.path());
    candidate.path_ = dir.path;
    if (candidate.path_ != world.path()) fail("noncanonical World path");
    const size_t split = dir.path.rfind('/');
    candidate.root_ = dir.path.substr(0, split);
    candidate.name_ = dir.path.substr(split + 1);
    Directory parent = directory(candidate.root_);
    candidate.rootDevice_ = parent.info.st_dev; candidate.rootInode_ = parent.info.st_ino;
    candidate.worldDevice_ = dir.info.st_dev; candidate.worldInode_ = dir.info.st_ino;
    struct stat named {};
    if (::fstatat(parent.fd.value, candidate.name_.c_str(), &named, AT_SYMLINK_NOFOLLOW)) systemFail("stat world in parent");
    identity(named, candidate.worldDevice_, candidate.worldInode_);
    candidate.seed_ = world.seed(); candidate.width_ = world.width(); candidate.height_ = world.height(); candidate.depth_ = world.depth();
    candidate.original_ = world.originalGeneration();
    candidate.source_ = readFile(dir.fd.value, "level.json", MaxMetadata);
    sourceIdentity(dir.fd.value, candidate.source_, world);
    candidate.sourceHash_ = hash(candidate.source_);
    candidate.persisted_ = readFile(dir.fd.value, "inventory.bin", MaxFile, true);
    const auto& bytes = candidate.persisted_;
    if (!bytes.empty()) {
        // LE: magic[8], version, seed, dimensions[3], levelHash, selected,
        // mainCount, armorCount. v3 adds u32 mode here, then all versions have
        // 40 (u32 keyBytes,u32 count,[v3 u32 damage],keyBytes) records.
        // v2/v3 append u32 legacyCount and unchanged (u32 originalSlot,keyBytes,
        // count,keyBytes) protected records. End with FNV32 of preceding bytes.
        if (bytes.size() < 368 || std::memcmp(bytes.data(), Magic, 8)) fail("bad inventory magic/size");
        Reader r {bytes, 8};
        const uint32_t version = r.u32();
        if ((version != 1 && version != 2 && version != Version) || r.u32() != candidate.seed_ || r.u32() != uint32_t(candidate.width_) ||
            r.u32() != uint32_t(candidate.height_) || r.u32() != uint32_t(candidate.depth_) || r.u32() != candidate.sourceHash_)
            fail("inventory version/world identity mismatch");
        const uint32_t selected = r.u32();
        if (selected >= HotbarSlots || r.u32() != MainSlots || r.u32() != ArmorSlots) fail("invalid inventory layout");
        candidate.selected_ = int(selected);
        if (version >= 3) {
            const uint32_t mode = r.u32();
            if (mode > uint32_t(GameMode::Creative)) fail("invalid game mode");
            candidate.mode_ = GameMode(mode);
        }
        auto readSlot = [&](bool withDamage) {
            const uint32_t length = r.u32(), count = r.u32(), damage = withDamage ? r.u32() : 0;
            if (length > 256 || count > MaxStack || damage > uint32_t(INT32_MAX) || r.at + length > bytes.size() - 4)
                fail("invalid slot record");
            Slot slot {std::string(bytes.begin() + r.at, bytes.begin() + r.at + length), int(count), int(damage)};
            r.at += length;
            return slot;
        };
        std::array<Slot, MainSlots + ArmorSlots> legacySlots {};
        for (int i = 0; i < MainSlots + ArmorSlots; ++i) {
            Slot slot = readSlot(version >= 3);
            if (version == 1) { validateLegacy(slot); legacySlots[i] = std::move(slot); }
            else {
                validate(slot);
                (i < MainSlots ? candidate.main_[i] : candidate.armor_[i - MainSlots]) = std::move(slot);
            }
        }
        if (version >= 2) {
            const uint32_t count = r.u32();
            if (count > MaxLegacyUnresolved) fail("too many protected legacy slots");
            std::set<int> originalSlots;
            for (uint32_t i = 0; i < count; ++i) {
                const uint32_t originalSlot = r.u32();
                if (originalSlot >= MainSlots + ArmorSlots || !originalSlots.insert(int(originalSlot)).second)
                    fail("invalid/duplicate protected legacy slot");
                Slot value = readSlot(false); // Protected v2 payload is unchanged in v3.
                LegacyUnresolvedSlot slot {int(originalSlot), std::move(value.key), value.count};
                validateUnresolved(slot); candidate.legacyUnresolved_.push_back(std::move(slot));
            }
        }
        if (r.at + 4 != bytes.size() || r.u32() != hash(bytes.data(), bytes.size() - 4)) fail("inventory checksum/length mismatch");
        if (version == 1) {
            // Preserve slot order and identity, not placement state. Do not merge
            // away another occupied legacy slot or silently clamp old counts.
            // A newly smaller source maxStack keeps the original slot up to its
            // limit and protects the exact remainder under its original key.
            for (int i = 0; i < MainSlots + ArmorSlots; ++i) {
                const Slot& old = legacySlots[i];
                if (!old.count) continue;
                const auto* spec = itemSpec(sourceitems::itemForBlockState(catalogIndex(old.key)));
                const int usable = spec && spec->maxStack >= 1 && spec->maxStack <= MaxStack ? std::min(old.count, spec->maxStack) : 0;
                if (usable) (i < MainSlots ? candidate.main_[i] : candidate.armor_[i - MainSlots]) = {spec->key, usable};
                if (old.count > usable) candidate.legacyUnresolved_.push_back({i, old.key, old.count - usable});
            }
        }
    }
    // Revalidate the named directories and exact bytes after parsing, so a
    // replacement during open cannot become the inventory's accepted baseline.
    Directory finalParent = directory(candidate.root_);
    identity(finalParent.info, candidate.rootDevice_, candidate.rootInode_);
    Directory finalDir = directory(candidate.path_);
    identity(finalDir.info, candidate.worldDevice_, candidate.worldInode_);
    if (readFile(finalDir.fd.value, "level.json", MaxMetadata) != candidate.source_ ||
        readFile(finalDir.fd.value, "inventory.bin", MaxFile, true) != candidate.persisted_)
        fail("world/inventory changed while opening");
    candidate.opened_ = true;
    *this = std::move(candidate);
}
void Inventory::save(const mcworld::World& world) {
    requireWorld(world);
    auto revalidate = [&]() {
        Directory parent = directory(root_); identity(parent.info, rootDevice_, rootInode_);
        Directory dir = directory(path_); identity(dir.info, worldDevice_, worldInode_);
        if (readFile(dir.fd.value, "level.json", MaxMetadata) != source_) fail("world source metadata changed");
        sourceIdentity(dir.fd.value, source_, world);
    };
    revalidate();
    Directory dir = directory(path_); identity(dir.info, worldDevice_, worldInode_);
    Bytes bytes(std::begin(Magic), std::end(Magic));
    put32(bytes, Version); put32(bytes, seed_); put32(bytes, uint32_t(width_)); put32(bytes, uint32_t(height_));
    put32(bytes, uint32_t(depth_)); put32(bytes, sourceHash_); put32(bytes, uint32_t(selected_));
    put32(bytes, MainSlots); put32(bytes, ArmorSlots);
    if (mode_ != GameMode::Survival && mode_ != GameMode::Creative) fail("invalid game mode");
    put32(bytes, uint32_t(mode_));
    auto append = [&](const Slot& slot) {
        validate(slot); put32(bytes, uint32_t(slot.key.size())); put32(bytes, uint32_t(slot.count));
        put32(bytes, uint32_t(slot.damage));
        bytes.insert(bytes.end(), slot.key.begin(), slot.key.end());
    };
    for (const auto& slot : main_) append(slot);
    for (const auto& slot : armor_) append(slot);
    if (legacyUnresolved_.size() > MaxLegacyUnresolved) fail("too many protected legacy slots");
    put32(bytes, uint32_t(legacyUnresolved_.size()));
    std::set<int> originalSlots;
    for (const auto& slot : legacyUnresolved_) {
        validateUnresolved(slot);
        if (!originalSlots.insert(slot.slot).second) fail("duplicate protected legacy slot");
        put32(bytes, uint32_t(slot.slot)); put32(bytes, uint32_t(slot.key.size())); put32(bytes, uint32_t(slot.count));
        bytes.insert(bytes.end(), slot.key.begin(), slot.key.end());
    }
    put32(bytes, hash(bytes));
    if (bytes.size() > MaxFile) fail("inventory exceeds bounded file size");
    atomicWrite(dir.fd.value, bytes, persisted_, revalidate);
}
bool Inventory::setGameMode(GameMode mode) {
    requireOpened();
    if (mode != GameMode::Survival && mode != GameMode::Creative) return false;
    mode_ = mode; return true;
}
bool Inventory::select(int slot) {
    requireOpened(); if (slot < 0 || slot >= HotbarSlots) return false;
    selected_ = slot; return true;
}
bool Inventory::transfer(int from, int to, int count) {
    requireOpened();
    if (from < 0 || from >= MainSlots || to < 0 || to >= MainSlots || from == to || count <= 0) return false;
    Slot& source = main_[from]; Slot& target = main_[to];
    if (!source.count) return false;
    const int requested = std::min(count, source.count);
    if (target.count && !mergeable(source, target)) {
        if (requested != source.count) return false;
        std::swap(source, target); return true;
    }
    const auto* spec = itemSpec(itemIndex(source.key));
    if (!spec) return false;
    const int moved = std::min(requested, stackLimit(*spec) - target.count);
    if (!moved) return false;
    if (!target.count) { target.key = source.key; target.damage = source.damage; }
    target.count += moved; source.count -= moved;
    if (!source.count) source = {};
    return true;
}
bool Inventory::belongsTo(const mcworld::World& world) const {
    return opened_ && world.loaded() && world.path() == path_ && world.seed() == seed_ &&
        world.width() == width_ && world.height() == height_ && world.depth() == depth_ &&
        world.originalGeneration() == original_;
}
bool Inventory::exchangeSlot(int slot, Slot& other, int count, bool intoOther) {
    requireOpened();
    if (slot < 0 || slot >= MainSlots || count <= 0) return false;
    // Refuse aliases into this inventory: the transaction expects an external slot.
    for (const auto& value : main_) if (&value == &other) return false;
    for (const auto& value : armor_) if (&value == &other) return false;
    try { validate(other); } catch (const std::runtime_error&) { return false; }
    Slot own = main_[slot], external = other; // All allocations precede commit.
    Slot& source = intoOther ? own : external;
    Slot& target = intoOther ? external : own;
    if (!source.count) return false;
    const int requested = std::min(count, source.count);
    if (target.count && !mergeable(source, target)) {
        if (requested != source.count) return false;
        std::swap(source, target);
    } else {
        const auto* spec = itemSpec(itemIndex(source.key));
        if (!spec) return false;
        const int moved = std::min(requested, stackLimit(*spec) - target.count);
        if (moved <= 0) return false;
        if (!target.count) target = {source.key, 0, source.damage};
        target.count += moved; source.count -= moved;
        if (!source.count) source = {};
    }
    std::swap(main_[slot], own); std::swap(other, external);
    return true;
}
void Inventory::clearMainAndArmor() {
    requireOpened();
    main_ = {};
    armor_ = {};
}
bool Inventory::add(int index, int count) {
    requireOpened();
    if (index < 0 || index >= legacy::kBlockCount) return false;
    const auto* spec = itemSpec(sourceitems::itemForBlockState(index));
    return spec && addItem(spec->itemId, spec->aux, count);
}
bool Inventory::canAddItem(int id, int aux, int count, int damage) const {
    requireOpened();
    if (count <= 0 || count > MainSlots * MaxStack) return false;
    const auto* spec = itemSpec(sourceitems::itemIndexForIdAux(id, aux));
    if (!spec || spec->maxStack < 1 || spec->maxStack > MaxStack || !validDamage(*spec, damage)) return false;
    const Slot incoming {spec->key, 1, damage};
    int space = 0;
    for (const auto& slot : main_)
        if (!slot.count || mergeable(slot, incoming)) space += stackLimit(*spec) - slot.count;
    return space >= count;
}
bool Inventory::addItem(int id, int aux, int count, int damage) {
    if (!canAddItem(id, aux, count, damage)) return false;
    const auto& spec = *itemSpec(sourceitems::itemIndexForIdAux(id, aux));
    const Slot incoming {spec.key, 1, damage};
    auto next = main_; // Allocate and distribute before committing anything.
    for (int pass = 0; pass < 2 && count; ++pass) for (auto& slot : next) {
        if ((pass == 0 && slot.count && mergeable(slot, incoming)) || (pass == 1 && !slot.count)) {
            const int moved = std::min(count, stackLimit(spec) - slot.count);
            slot.key = incoming.key; slot.damage = damage; slot.count += moved; count -= moved;
            if (!count) break;
        }
    }
    main_.swap(next); return true;
}
bool Inventory::consumeSelected(int count) {
    requireOpened(); Slot& slot = main_[selected_];
    if (count <= 0 || count > slot.count) return false;
    slot.count -= count; if (!slot.count) slot = {}; return true;
}
bool Inventory::damageSelected(int amount) {
    requireOpened();
    Slot& slot = main_[selected_];
    const int maximum = selectedMaxDamage();
    if (amount <= 0 || !slot.count || maximum <= 0) return false;
    if (mode_ == GameMode::Creative) return true;
    // ItemInstance::hurt uses >, not >=. Subtraction also avoids int overflow.
    if (amount > maximum - slot.damage) slot = {};
    else slot.damage += amount;
    return true;
}
bool Inventory::equipSelectedArmor() {
    requireOpened();
    Slot& hand = main_[selected_];
    const auto* spec = hand.count ? itemSpec(itemIndex(hand.key)) : nullptr;
    const auto* armor = spec ? mcgame::toolProperties(spec->itemId) : nullptr;
    if (!armor || armor->kind != mcgame::ToolKind::Armor || armor->armorSlot < 0 || armor->armorSlot > 3) return false;
    std::swap(hand, armor_[3 - armor->armorSlot]);
    return true;
}
void Inventory::hurtArmor(float damage) {
    requireOpened();
    if (mode_ == GameMode::Creative || !std::isfinite(damage) || damage <= 0) return;
    // Inventory.cpp:641-659: dmg = dmg / 4; if (dmg < 1) dmg = 1; hurtAndBreak((int) dmg).
    const int wear = int(std::max(1.0f, damage / 4));
    for (Slot& slot : armor_) {
        const auto* spec = slot.count ? itemSpec(itemIndex(slot.key)) : nullptr;
        const auto* armor = spec ? mcgame::toolProperties(spec->itemId) : nullptr;
        if (!armor || armor->kind != mcgame::ToolKind::Armor || armor->maxDamage <= 0) continue;
        if (wear > armor->maxDamage - slot.damage) slot = {};
        else slot.damage += wear;
    }
}
int Inventory::armorValue() const {
    int total = 0;
    for (const Slot& slot : armor_) {
        const auto* spec = slot.count ? itemSpec(itemIndex(slot.key)) : nullptr;
        const auto* armor = spec ? mcgame::toolProperties(spec->itemId) : nullptr;
        if (armor && armor->kind == mcgame::ToolKind::Armor) total += armor->defense;
    }
    return total;
}
int Inventory::selectedDamage() const { return main_[selected_].damage; }
int Inventory::selectedMaxDamage() const { return maxDamage(itemSpec(selectedItemIndex())); }
bool Inventory::selectHotbarItem(int slot, int index, int count) {
    requireOpened();
    const auto* spec = itemSpec(index);
    if (mode_ != GameMode::Creative || slot < 0 || slot >= HotbarSlots || !spec || !spec->creative ||
        count < 1 || count > stackLimit(*spec) || count > MaxStack) return false;
    Slot next {spec->key, count}; main_[slot] = std::move(next); return true;
}
bool Inventory::selectHotbarBlock(int slot, const std::string& key, int count) {
    return selectHotbarBlock(slot, catalogIndex(key), count);
}
bool Inventory::selectHotbarBlock(int slot, int index, int count) {
    requireOpened();
    if (index < 0 || index >= legacy::kBlockCount) return false;
    return selectHotbarItem(slot, sourceitems::itemForBlockState(index), count);
}
int Inventory::selectedItemIndex() const { return itemIndex(main_[selected_].key); }
int Inventory::selectedItemId() const {
    const auto* spec = itemSpec(selectedItemIndex()); return spec ? spec->itemId : -1;
}
int Inventory::selectedAux() const {
    const auto* spec = itemSpec(selectedItemIndex()); return spec ? spec->aux : -1;
}
int Inventory::selectedPlaceData() const {
    const auto* spec = itemSpec(selectedItemIndex()); return spec && spec->placeable ? spec->defaultData : -1;
}
int Inventory::selectedBlockIndex() const {
    const auto* spec = itemSpec(selectedItemIndex()); return spec ? spec->renderIndex : -1;
}
int Inventory::selectedCount() const { return main_[selected_].count; }
std::string Inventory::infoJson() const {
    std::ostringstream out; out.imbue(std::locale::classic());
    out << "{\"opened\":" << (opened_ ? "true" : "false") << ",\"selected\":" << selected_ << ",\"selectedSlot\":" << selected_
        << ",\"selectedBlockIndex\":" << selectedBlockIndex() << ",\"selectedItemIndex\":" << selectedItemIndex()
        << ",\"selectedItemId\":" << selectedItemId() << ",\"selectedAux\":" << selectedAux()
        << ",\"selectedPlaceData\":" << selectedPlaceData() << ",\"selectedCount\":" << selectedCount()
        << ",\"selectedDamage\":" << selectedDamage() << ",\"selectedMaxDamage\":" << selectedMaxDamage()
        << ",\"gameMode\":\"" << (mode_ == GameMode::Survival ? "survival" : "creative") << '"'
        << ",\"emptyHand\":" << (emptyHand() ? "true" : "false")
        << ",\"catalogKind\":\"source-items\",\"maxStack\":64,\"craftingImplemented\":true,\"armorStatsImplemented\":true,\"slots\":[";
    auto append = [&](const Slot& slot) {
        const int index = itemIndex(slot.key);
        const auto* spec = itemSpec(index);
        const int maximum = maxDamage(spec);
        out << "{\"key\":" << quote(slot.key) << ",\"canonicalKey\":" << quote(slot.key) << ",\"count\":" << slot.count
            << ",\"damage\":" << slot.damage << ",\"maxDamage\":" << maximum << ",\"remaining\":" << maximum - slot.damage
            << ",\"itemIndex\":" << index << ",\"itemId\":" << (spec ? spec->itemId : -1) << ",\"aux\":" << (spec ? spec->aux : -1)
            << ",\"index\":" << (spec ? spec->renderIndex : -1) << ",\"maxStack\":" << (spec ? spec->maxStack : 0)
            << ",\"name\":" << quote(spec ? spec->name : "") << '}';
    };
    for (size_t i = 0; i < main_.size(); ++i) { if (i) out << ','; append(main_[i]); }
    out << "],\"armor\":[";
    for (size_t i = 0; i < armor_.size(); ++i) { if (i) out << ','; append(armor_[i]); }
    out << "],\"invalidLegacySlots\":[";
    auto appendLegacy = [&]() {
        for (size_t i = 0; i < legacyUnresolved_.size(); ++i) {
            if (i) out << ',';
            const auto& slot = legacyUnresolved_[i];
            const bool mapped = itemSpec(sourceitems::itemForBlockState(catalogIndex(slot.key))) != nullptr;
            out << "{\"slot\":" << slot.slot << ",\"key\":" << quote(slot.key) << ",\"count\":" << slot.count
                << ",\"reason\":\"" << (mapped ? "exceeds-item-max-stack" : "no-source-item") << "\",\"protected\":true}";
        }
    };
    appendLegacy(); out << "],\"legacyUnresolved\":["; appendLegacy();
    out << "],\"migrationWarning\":";
    if (legacyUnresolved_.empty()) out << "null";
    else out << quote("Legacy inventory data is protected, not discarded. Unresolved states/counts cannot be selected or placed.");
    out << '}'; return out.str();
}
} // namespace mcinventory
