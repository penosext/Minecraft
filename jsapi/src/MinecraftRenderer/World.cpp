#include "World.hpp"
#include "OriginalWorld.hpp"
#include "ItemPlacement.hpp"
#include "BlockInteraction.hpp"
#include "BlockCollision.hpp"
#include "ItemCatalog.hpp"
#include "LegacyCatalog.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace mcworld {
namespace {
constexpr size_t Cells = size_t(Width) * Height * Depth;
constexpr float Radius = .25f, BodyHeight = 1.8f, Epsilon = .0001f;
constexpr char Format[] = "mcworld-finite";
constexpr uint32_t Version = 1;
constexpr char OriginalFormat[] = "mcworld-original-terrain";
constexpr char OriginalGenerator[] = "original-biomes-density-surfaces-v1";
constexpr char StructuresGenerator[] = "original-biomes-density-surfaces-structures-v2";
constexpr char DecorationGenerator[] = "original-biomes-density-surfaces-structures-decoration-v3";
constexpr char DecorationLimitations[] = "decoration-feature-subset-not-full-BiomeDecorator-postProcess;no-caves-loot-villagers-full-physics;cube-DDA-not-exact-render-shapes;catalog-representative-orientation-state-content-fallback";
// v4 = v3 + original getChunk caves/canyons + decorateOres. v1..v3 saves keep
// their generator string and are regenerated without carvers/ores forever.
constexpr char CarversGenerator[] = "original-biomes-density-surfaces-carvers-structures-ores-decoration-v4";
constexpr char CarversLimitations[] = "decoration-subset-not-full-BiomeDecorator-postProcess;ores-own-rng-not-original-positions;no-lakes-dungeons-mineshafts-strongholds-loot-villagers-full-physics;cube-DDA-not-exact-render-shapes;catalog-representative-orientation-state-content-fallback";
constexpr uint32_t OriginalEditVersion = 2;
constexpr uint32_t OriginalSaveV2 = 2;
constexpr size_t OriginalV1Fields = 14;
const unsigned char DimensionMagic[8] = {'M','C','O','D','I','M','S',0};
constexpr size_t MaxDimensionFile = 24 + 16 * 4;
constexpr int OverworldSlot = 0, MaxDimensionSlot = 15;
constexpr char TerrainLimitations[] = "no-caves-decoration-trees-ores-structures";
constexpr char StructuresLimitations[] = "no-caves-decoration-trees-ores-loot-villagers-full-physics;catalog-representative-orientation-state-content-fallback";
const char* generatorName(const OriginalWorld& w) {
    return w.carversEnabled() ? CarversGenerator : w.decorationsEnabled() ? DecorationGenerator
         : w.structuresEnabled() ? StructuresGenerator : OriginalGenerator;
}
const char* generatorLimitations(const OriginalWorld& w) {
    return w.carversEnabled() ? CarversLimitations : w.decorationsEnabled() ? DecorationLimitations
         : w.structuresEnabled() ? StructuresLimitations : TerrainLimitations;
}
constexpr char SourceCommit[] = "09df8928eeb15328bca499d225e48ea95af6d119";
const unsigned char OriginalChunkMagic[8] = {'M','C','O','C','H','N','K',0};
const unsigned char OriginalStateMagic[8] = {'M','C','O','S','T','A','T',0};
constexpr size_t MaxOriginalChunk = 288 + size_t(OriginalWorld::ChunkCells) * 8;
constexpr size_t MaxOriginalEdits = 1024 * 1024;
constexpr size_t MaxMetadata = 1024 * 1024;
const unsigned char ChunkMagic[8] = {'M','C','W','C','H','N','K',0};
const unsigned char StateMagic[8] = {'M','C','W','S','T','A','T',0};
const unsigned char EditMagic[8] = {'M','C','W','E','D','I','T',0};
constexpr uint32_t EditVersion = 1;
constexpr size_t MaxEdits = 24 + Cells * 8;
std::atomic<uint64_t> revisionCounter {0};
uint64_t nextRevision() { return ++revisionCounter; }
using Bytes = std::vector<unsigned char>;

[[noreturn]] void fail(const std::string& message) { throw std::runtime_error("World: " + message); }
[[noreturn]] void systemFail(const std::string& message) { fail(message + ": " + std::strerror(errno)); }

std::string dimensionDirectoryName(int slot) {
    if (slot == OverworldSlot) return {};
    if (slot < 0 || slot > MaxDimensionSlot) fail("invalid dimension slot");
    return "DIM-" + std::to_string(slot);
}
bool dimensionDirectoryCandidate(const std::string& name) {
    if (name.compare(0, 4, "DIM-") != 0 || name.size() < 5 || name.size() > 6) return false;
    for (size_t i = 4; i < name.size(); ++i) if (name[i] < '0' || name[i] > '9') return false;
    return name[4] != '0';
}

struct FD {
    int value = -1;
    explicit FD(int value = -1) : value(value) {}
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

Directory directory(const std::string& path, bool createFinal = false) {
    if (path.empty() || path[0] != '/' || path.find('\0') != std::string::npos || path.size() > 4096)
        fail("expected an absolute bounded directory path");
    std::vector<std::string> parts;
    for (size_t begin = 1; begin < path.size();) {
        size_t end = path.find('/', begin);
        if (end == std::string::npos) end = path.size();
        const std::string part = path.substr(begin, end - begin);
        if (part == "." || part == "..") fail("dot path components are forbidden");
        if (!part.empty()) parts.push_back(part);
        begin = end + 1;
    }
    if (parts.empty()) fail("filesystem root cannot be a world root");
    Directory result;
    result.fd = FD(::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (result.fd.value < 0) systemFail("open filesystem root");
    for (size_t i = 0; i < parts.size(); ++i) {
        int fd = ::openat(result.fd.value, parts[i].c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (fd < 0 && errno == ENOENT && createFinal && i + 1 == parts.size()) {
            if (::mkdirat(result.fd.value, parts[i].c_str(), 0700) != 0 && errno != EEXIST)
                systemFail("create save root");
            fd = ::openat(result.fd.value, parts[i].c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        }
        if (fd < 0) systemFail("open directory " + parts[i]);
        result.fd = FD(fd);
        result.path += "/" + parts[i];
    }
    if (::fstat(result.fd.value, &result.info) != 0) systemFail("stat directory");
    return result;
}
bool beneath(const std::string& path, const std::string& root) {
    return path.size() > root.size() && path.compare(0, root.size(), root) == 0 && path[root.size()] == '/';
}
void identity(const struct stat& info, uint64_t device, uint64_t inode) {
    if (uint64_t(info.st_dev) != device || uint64_t(info.st_ino) != inode)
        fail("save directory was replaced");
}
void regularTarget(int dir, const std::string& name, bool mayBeMissing) {
    struct stat info {};
    if (::fstatat(dir, name.c_str(), &info, AT_SYMLINK_NOFOLLOW) != 0) {
        if (mayBeMissing && errno == ENOENT) return;
        systemFail("stat " + name);
    }
    if (!S_ISREG(info.st_mode) || info.st_nlink != 1) fail(name + " must be a regular, nonlinked file");
}
Bytes readFile(int dir, const std::string& name, size_t maximum) {
    FD fd(::openat(dir, name.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
    if (fd.value < 0) systemFail("open " + name);
    struct stat info {};
    if (::fstat(fd.value, &info) != 0) systemFail("stat " + name);
    if (!S_ISREG(info.st_mode) || info.st_nlink != 1 || info.st_size < 0 || uint64_t(info.st_size) > maximum)
        fail("invalid type or oversized " + name);
    Bytes bytes(size_t(info.st_size));
    size_t offset = 0;
    while (offset < bytes.size()) {
        const ssize_t count = ::read(fd.value, bytes.data() + offset, bytes.size() - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) fail("truncated or unreadable " + name);
        offset += size_t(count);
    }
    unsigned char extra;
    ssize_t count;
    do { count = ::read(fd.value, &extra, 1); } while (count < 0 && errno == EINTR);
    if (count != 0) fail("file changed or unreadable " + name);
    return bytes;
}
Bytes readOptionalFile(int dir, const std::string& name, size_t maximum) {
    struct stat info {};
    if (::fstatat(dir, name.c_str(), &info, AT_SYMLINK_NOFOLLOW) != 0) {
        if (errno == ENOENT) return {};
        systemFail("stat " + name);
    }
    return readFile(dir, name, maximum);
}
std::atomic<uint64_t> temporaryCounter {0};
void atomicWrite(int dir, const std::string& name, const Bytes& bytes, bool replace, bool exclusiveCommit = false) {
    regularTarget(dir, name, true);
    if (!replace) {
        struct stat info {};
        if (::fstatat(dir, name.c_str(), &info, AT_SYMLINK_NOFOLLOW) == 0 || errno != ENOENT)
            fail("refusing to overwrite " + name);
    }
    const std::string temp = "." + name + ".tmp-" + std::to_string(::getpid()) + "-" +
                             std::to_string(++temporaryCounter);
    FD fd(::openat(dir, temp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
    if (fd.value < 0) systemFail("create temporary " + name);
    try {
        size_t offset = 0;
        while (offset < bytes.size()) {
            const ssize_t count = ::write(fd.value, bytes.data() + offset, bytes.size() - offset);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) systemFail("write " + name);
            offset += size_t(count);
        }
        if (::fsync(fd.value) != 0) systemFail("flush " + name);
        const int raw = fd.value;
        fd.value = -1;
        if (::close(raw) != 0) systemFail("close " + name);
        regularTarget(dir, name, true);
        if (!replace && exclusiveCommit) {
            if (::linkat(dir, temp.c_str(), dir, name.c_str(), 0) != 0) systemFail("commit exclusive " + name);
            if (::unlinkat(dir, temp.c_str(), 0) != 0) systemFail("remove committed temporary " + name);
        } else if (::renameat(dir, temp.c_str(), dir, name.c_str()) != 0) systemFail("commit " + name);
        if (::fsync(dir) != 0) systemFail("flush save directory");
    } catch (...) {
        ::unlinkat(dir, temp.c_str(), 0);
        throw;
    }
}
uint32_t hash(const unsigned char* bytes, size_t size) {
    uint32_t result = 2166136261u;
    for (size_t i = 0; i < size; ++i) result = (result ^ bytes[i]) * 16777619u;
    return result;
}
uint32_t hash(const Bytes& bytes) { return hash(bytes.data(), bytes.size()); }
void put32(Bytes& bytes, uint32_t value) {
    for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<unsigned char>(value >> (8 * i)));
}
void putFloat(Bytes& bytes, float value) {
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559, "IEEE float required");
    uint32_t bits; std::memcpy(&bits, &value, 4); put32(bytes, bits);
}
struct Reader {
    const Bytes& bytes; size_t offset = 0;
    void magic(const unsigned char* value) {
        if (bytes.size() < offset + 8 || std::memcmp(bytes.data() + offset, value, 8)) fail("bad file magic");
        offset += 8;
    }
    uint32_t u32() {
        if (bytes.size() < offset + 4) fail("truncated header");
        uint32_t value = 0;
        for (int i = 0; i < 4; ++i) value |= uint32_t(bytes[offset++]) << (i * 8);
        return value;
    }
    float number() {
        uint32_t bits = u32(); float value; std::memcpy(&value, &bits, 4);
        if (!std::isfinite(value)) fail("nonfinite player state");
        return value;
    }
};
std::vector<uint32_t> readEdits(int dir, uint32_t chunkHash) {
    struct stat info {};
    if (::fstatat(dir, "edits.bin", &info, AT_SYMLINK_NOFOLLOW) != 0) {
        if (errno == ENOENT) return {};
        systemFail("stat edits.bin");
    }
    const Bytes bytes = readFile(dir, "edits.bin", MaxEdits);
    if (bytes.size() < 24) fail("truncated edits");
    Reader reader {bytes}; reader.magic(EditMagic);
    if (reader.u32() != EditVersion || reader.u32() != chunkHash) fail("invalid edits header");
    const uint32_t count = reader.u32();
    if (count > Cells || bytes.size() != 24 + size_t(count) * 8) fail("invalid edits size");
    std::vector<uint32_t> indices;
    indices.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t index = reader.u32(), value = reader.u32();
        if (index >= Cells || value != 0 || (!indices.empty() && index <= indices.back()))
            fail("invalid, duplicate or unordered edit");
        indices.push_back(index);
    }
    if (reader.u32() != hash(bytes.data(), bytes.size() - 4)) fail("edits checksum mismatch");
    return indices;
}
Bytes encodeEdits(uint32_t chunkHash, const std::vector<uint32_t>& indices) {
    Bytes bytes(std::begin(EditMagic), std::end(EditMagic));
    bytes.reserve(24 + indices.size() * 8);
    put32(bytes, EditVersion); put32(bytes, chunkHash); put32(bytes, uint32_t(indices.size()));
    for (uint32_t index : indices) { put32(bytes, index); put32(bytes, 0); }
    put32(bytes, hash(bytes));
    return bytes;
}
Bytes encodeDimensions(uint32_t metadataHash, const std::vector<int>& slots) {
    Bytes bytes(std::begin(DimensionMagic), std::end(DimensionMagic));
    put32(bytes, OriginalSaveV2); put32(bytes, metadataHash); put32(bytes, uint32_t(slots.size()));
    for (int slot : slots) put32(bytes, uint32_t(slot));
    put32(bytes, hash(bytes));
    return bytes;
}
std::vector<int> decodeDimensions(const Bytes& bytes, uint32_t metadataHash) {
    if (bytes.size() < 24) fail("truncated dimension manifest");
    Reader reader {bytes}; reader.magic(DimensionMagic);
    if (reader.u32() != OriginalSaveV2 || reader.u32() != metadataHash) fail("invalid dimension manifest header");
    const uint32_t count = reader.u32();
    if (!count || count > uint32_t(MaxDimensionSlot) + 1 || bytes.size() != 24 + size_t(count) * 4)
        fail("invalid dimension manifest size");
    std::vector<int> slots;
    slots.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t slot = reader.u32();
        if (slot > uint32_t(MaxDimensionSlot)) fail("dimension slot out of range");
        if (!slots.empty() && int(slot) <= slots.back()) fail("unordered/duplicate dimension slot");
        slots.push_back(int(slot));
    }
    if (slots.front() != OverworldSlot) fail("dimension manifest omits the overworld");
    if (reader.u32() != hash(bytes.data(), bytes.size() - 4)) fail("dimension manifest checksum mismatch");
    return slots;
}
bool breakable(uint16_t block) {
    if (!block) return false;
    const auto& spec = legacy::kBlockSpecs[block - 1];
    return std::strcmp(spec.key, "unbreakable") != 0 &&
           std::strcmp(spec.shape, "liquid") != 0 && std::strcmp(spec.shape, "air") != 0;
}
std::string quote(const std::string& text) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') out << '\\' << c;
        else if (c < 32 || c == 127) out << "\\u00" << "0123456789abcdef"[c >> 4] << "0123456789abcdef"[c & 15];
        else out << c;
    }
    out << '"';
    return out.str();
}

struct Json {
    enum Kind { String, Number, Array, Object } kind = Number;
    std::string text;
    std::vector<Json> array;
    std::map<std::string, Json> object;
};
class JsonParser {
public:
    explicit JsonParser(const std::string& text) : text_(text) {}
    Json parse() { Json value = read(0); whitespace(); if (at_ != text_.size()) fail("trailing JSON data"); return value; }
private:
    const std::string& text_; size_t at_ = 0;
    void whitespace() { while (at_ < text_.size() && (text_[at_] == ' ' || text_[at_] == '\n' || text_[at_] == '\r' || text_[at_] == '\t')) ++at_; }
    char take() { if (at_ == text_.size()) fail("truncated JSON"); return text_[at_++]; }
    std::string string() {
        if (take() != '"') fail("expected JSON string");
        std::string result;
        for (;;) {
            unsigned char c = static_cast<unsigned char>(take());
            if (c == '"') return result;
            if (c < 32 || c >= 127) fail("metadata strings must be ASCII");
            if (c == '\\') {
                c = static_cast<unsigned char>(take());
                if (c == 'u') {
                    unsigned int value = 0;
                    for (int i = 0; i < 4; ++i) {
                        char digit = take(); int n = digit >= '0' && digit <= '9' ? digit - '0' :
                            digit >= 'a' && digit <= 'f' ? digit - 'a' + 10 : digit >= 'A' && digit <= 'F' ? digit - 'A' + 10 : -1;
                        if (n < 0) fail("invalid JSON escape");
                        value = value * 16 + unsigned(n);
                    }
                    if (value < 32 || value >= 127) fail("non-ASCII metadata escape");
                    c = static_cast<unsigned char>(value);
                } else if (c != '"' && c != '\\' && c != '/') fail("invalid JSON escape");
            }
            result.push_back(char(c));
            if (result.size() > 256) fail("metadata string too long");
        }
    }
    Json read(int depth) {
        if (depth > 8) fail("JSON nesting too deep");
        whitespace();
        if (at_ == text_.size()) fail("empty JSON value");
        Json value;
        if (text_[at_] == '"') { value.kind = Json::String; value.text = string(); return value; }
        if (text_[at_] == '[') {
            value.kind = Json::Array; ++at_; whitespace();
            if (at_ < text_.size() && text_[at_] == ']') { ++at_; return value; }
            for (;;) {
                value.array.push_back(read(depth + 1));
                if (value.array.size() > 65535) fail("metadata array too large");
                whitespace(); char c = take(); if (c == ']') return value; if (c != ',') fail("invalid JSON array");
            }
        }
        if (text_[at_] == '{') {
            value.kind = Json::Object; ++at_; whitespace();
            if (at_ < text_.size() && text_[at_] == '}') { ++at_; return value; }
            for (;;) {
                whitespace(); std::string key = string(); whitespace(); if (take() != ':') fail("invalid JSON object");
                if (!value.object.emplace(key, read(depth + 1)).second) fail("duplicate JSON key");
                if (value.object.size() > 32) fail("too many metadata fields");
                whitespace(); char c = take(); if (c == '}') return value; if (c != ',') fail("invalid JSON object");
            }
        }
        const size_t start = at_;
        while (at_ < text_.size() && text_[at_] >= '0' && text_[at_] <= '9') ++at_;
        if (at_ == start || (at_ - start > 1 && text_[start] == '0') || at_ - start > 10) fail("invalid metadata integer");
        value.text = text_.substr(start, at_ - start); return value;
    }
};
const Json& field(const Json& object, const std::string& name, Json::Kind kind) {
    auto it = object.object.find(name);
    if (object.kind != Json::Object || it == object.object.end() || it->second.kind != kind) fail("missing/invalid metadata " + name);
    return it->second;
}
uint32_t integer(const Json& value) {
    if (value.kind != Json::Number) fail("expected integer");
    uint64_t result = 0;
    for (char c : value.text) result = result * 10 + unsigned(c - '0');
    if (result > UINT32_MAX) fail("metadata integer overflow");
    return uint32_t(result);
}
uint32_t integer(const Json& object, const std::string& name) { return integer(field(object, name, Json::Number)); }
std::vector<std::string> catalog() {
    if (legacy::kBlockCount < 1 || legacy::kBlockCount > 65535) fail("invalid embedded catalog size");
    std::vector<std::string> keys;
    std::set<std::string> unique;
    for (int i = 0; i < legacy::kBlockCount; ++i) {
        const std::string key = legacy::kBlockSpecs[i].key;
        if (key.empty() || key.size() > 256 || !unique.insert(key).second) fail("invalid embedded catalog key");
        keys.push_back(key);
    }
    return keys;
}
uint16_t id(const std::string& key) {
    for (int i = 0; i < legacy::kBlockCount; ++i) if (key == legacy::kBlockSpecs[i].key) return uint16_t(i + 1);
    fail("required terrain key absent: " + key);
}
uint32_t paletteHash(const std::vector<std::string>& keys) {
    Bytes bytes;
    for (const auto& key : keys) { bytes.insert(bytes.end(), key.begin(), key.end()); bytes.push_back(0); }
    return hash(bytes);
}
std::string originalChunkName(int key) { return "chunk-" + std::to_string(key) + ".bin"; }
std::set<std::string> dimensionDirectories(int dir) {
    FD duplicate(::openat(dir, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (duplicate.value < 0) systemFail("open world directory listing");
    DIR* listing = ::fdopendir(duplicate.value);
    if (!listing) systemFail("read world directory listing");
    duplicate.value = -1;
    std::set<std::string> result;
    try {
        for (;;) {
            errno = 0;
            auto* entry = ::readdir(listing);
            if (!entry) { if (errno) systemFail("list dimensions"); break; }
            const std::string name = entry->d_name;
            if (!dimensionDirectoryCandidate(name)) continue;
            struct stat info {};
            if (::fstatat(dir, name.c_str(), &info, AT_SYMLINK_NOFOLLOW) != 0) systemFail("stat " + name);
            if (!S_ISDIR(info.st_mode)) fail("dimension store must be a real directory: " + name);
            if (result.size() > size_t(MaxDimensionSlot)) fail("too many dimension stores");
            result.insert(name);
        }
        ::closedir(listing);
    } catch (...) { ::closedir(listing); throw; }
    return result;
}
std::map<int, Bytes> originalChunkFiles(int dir, int size) {
    FD duplicate(::openat(dir, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (duplicate.value < 0) systemFail("open chunk directory listing");
    DIR* listing = ::fdopendir(duplicate.value);
    if (!listing) systemFail("read chunk directory listing");
    duplicate.value = -1;
    std::map<int, Bytes> result;
    size_t records = 0;
    try {
        for (;;) {
            errno = 0;
            auto* entry = ::readdir(listing);
            if (!entry) { if (errno) systemFail("list chunks"); break; }
            const std::string name = entry->d_name;
            if (name.compare(0, 6, "chunk-") != 0) continue;
            const size_t end = name.find(".bin", 6);
            if (end == std::string::npos || end + 4 != name.size() || end == 6 || end - 6 > 6)
                fail("invalid original chunk filename");
            int key = 0;
            for (size_t i = 6; i < end; ++i) {
                if (name[i] < '0' || name[i] > '9') fail("invalid original chunk filename");
                key = key * 10 + name[i] - '0';
            }
            if (key >= (size / 16) * (size / 16) || name != originalChunkName(key)) fail("original chunk outside world");
            Bytes bytes = readFile(dir, name, MaxOriginalChunk);
            if (bytes.size() < 288 || (bytes.size() - 288) % 8) fail("invalid original chunk length");
            records += (bytes.size() - 288) / 8;
            if (records > MaxOriginalEdits) fail("too many saved original edits");
            result.emplace(key, std::move(bytes));
        }
        ::closedir(listing);
    } catch (...) { ::closedir(listing); throw; }
    return result;
}
OriginalWorld::ModifiedChunk decodeOriginalChunk(const Bytes& bytes, int key, uint32_t seed,
                                                 uint32_t metadataHash, const std::vector<uint16_t>& remap) {
    Reader reader {bytes}; reader.magic(OriginalChunkMagic);
    const uint32_t version = reader.u32();
    if ((version != Version && version != OriginalEditVersion) || reader.u32() != seed || reader.u32() != metadataHash || reader.u32() != uint32_t(key))
        fail("invalid original chunk header");
    const uint32_t count = reader.u32();
    if ((!count && version == Version) || count > OriginalWorld::ChunkCells || bytes.size() != 288 + size_t(count) * 8)
        fail("invalid original edit count");
    OriginalWorld::ModifiedChunk changes; changes.formatVersion = version;
    for (auto& biome : changes.biomes) {
        biome = bytes[reader.offset++];
        if (biome >= 24) fail("invalid saved biome id");
    }
    uint32_t previous = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const auto index = reader.u32(), value = reader.u32();
        if (index >= OriginalWorld::ChunkCells || value >= remap.size() || (version == Version && value != 0) || (i && index <= previous))
            fail("invalid original edit");
        changes.edits.emplace(index, remap[value]); previous = index;
    }
    if (reader.u32() != hash(bytes.data(), bytes.size() - 4)) fail("original chunk checksum mismatch");
    changes.persistedHash = hash(bytes); changes.persisted = true;
    return changes;
}
Bytes encodeOriginalChunk(const OriginalWorld::ModifiedChunk& changes, int key, uint32_t seed,
                          uint32_t metadataHash, const std::vector<uint16_t>& savedIds) {
    Bytes bytes(std::begin(OriginalChunkMagic), std::end(OriginalChunkMagic));
    put32(bytes, OriginalEditVersion); put32(bytes, seed); put32(bytes, metadataHash); put32(bytes, uint32_t(key));
    put32(bytes, uint32_t(changes.edits.size()));
    bytes.insert(bytes.end(), changes.biomes.begin(), changes.biomes.end());
    for (const auto& edit : changes.edits) {
        if (edit.second >= savedIds.size() || (edit.second && !savedIds[edit.second])) fail("edited key missing from saved palette");
        put32(bytes, edit.first); put32(bytes, savedIds[edit.second]);
    }
    put32(bytes, hash(bytes)); return bytes;
}
size_t cell(int x, int y, int z) { return (size_t(y) * Depth + z) * Width + x; }
uint32_t mix(uint32_t value) {
    value ^= value >> 16; value *= 0x7feb352du; value ^= value >> 15; value *= 0x846ca68bu; return value ^ (value >> 16);
}
int lattice(uint32_t seed, int x, int z) {
    return int(mix(seed ^ uint32_t(x) * 0x9e3779b9u ^ uint32_t(z) * 0x85ebca6bu) & 65535u) - 32768;
}
int smooth(int t) { return int((int64_t(t) * t / 65536) * (196608 - 2 * t) / 65536); }
int lerp(int a, int b, int t) { return a + int(int64_t(b - a) * t / 65536); }
int noise(uint32_t seed, int x, int z, int scale) {
    int gx = x / scale, gz = z / scale;
    int tx = smooth((x % scale) * 65536 / scale), tz = smooth((z % scale) * 65536 / scale);
    return lerp(lerp(lattice(seed, gx, gz), lattice(seed, gx + 1, gz), tx),
                lerp(lattice(seed, gx, gz + 1), lattice(seed, gx + 1, gz + 1), tx), tz);
}
} // namespace

void World::requireLoaded() const { if (!loaded_) fail("no world loaded"); }
const std::string& World::path() const { requireLoaded(); return path_; }
uint32_t World::seed() const { requireLoaded(); return seed_; }
float World::playerX() const { requireLoaded(); return x_; }
float World::playerY() const { requireLoaded(); return y_; }
float World::playerZ() const { requireLoaded(); return z_; }
int World::blockCount() const { requireLoaded(); return original_ ? original_->windowBlockCount(x_, z_) : blockCount_; }
uint64_t World::revision() const {
    requireLoaded();
    if (original_ && observedStreamingRevision_ != original_->streamingRevision()) {
        observedStreamingRevision_ = original_->streamingRevision(); revision_ = nextRevision();
    }
    return revision_;
}
void World::setRenderRadius(int radius) {
    if (radius < 1 || radius > OriginalWorld::MaxRadius) fail("render radius must be in 1..12");
    setChunkRanges(radius, radius);
}
void World::setChunkRanges(int displayRadius, int preloadRadius) {
    if (displayRadius < 1 || displayRadius > OriginalWorld::MaxRadius || preloadRadius < displayRadius || preloadRadius > OriginalWorld::MaxRadius)
        fail("chunk ranges must satisfy 1 <= display <= preload <= 12");
    if (original_) original_->setChunkRanges(displayRadius, preloadRadius);
    renderRadius_ = displayRadius; preloadRadius_ = preloadRadius;
}
void World::configureOriginal() {
    if (!original_) return;
    if (streaming_) original_->beginStreaming(renderRadius_);
    if (renderRadius_) original_->setChunkRanges(renderRadius_, preloadRadius_);
}
void World::loadPlayerSafety() {
    if (!original_) return;
    for (int z : {int(std::floor(z_ - Radius)), int(std::floor(z_ + Radius))})
        for (int x : {int(std::floor(x_ - Radius)), int(std::floor(x_ + Radius))})
            if (x >= 0 && x < original_->size() && z >= 0 && z < original_->size()) original_->loadChunk(x, z);
}
void World::beginStreaming(int radius) {
    setRenderRadius(radius); streaming_ = true;
    if (original_) {
        original_->beginStreaming(radius);
        original_->setStreamingCenter(x_, z_);
        loadPlayerSafety();
    }
}
void World::endStreaming() {
    streaming_ = false;
    if (original_) original_->endStreaming();
}
int World::processChunkLoads(int budget) {
    requireLoaded();
    if (budget < 0) fail("negative chunk load budget");
    if (!original_ || !streaming_) return 0;
    original_->setStreamingCenter(x_, z_);
    return original_->processChunkLoads(budget);
}
std::vector<ChunkRequest> World::chunkRequests(int budget) const {
    requireLoaded();
    if (budget < 0) fail("negative chunk load budget");
    return original_ && streaming_ ? original_->chunkRequests(budget) : std::vector<ChunkRequest>{};
}
bool World::commitChunk(GeneratedChunk&& chunk) {
    requireLoaded();
    if (!original_ || !streaming_) return false;
    return original_->commitChunk(std::move(chunk));
}
void World::setStreamingView(float yaw, float pitch, float aspect) {
    requireLoaded();
    if (!std::isfinite(yaw) || !std::isfinite(pitch) || !std::isfinite(aspect) || aspect <= 0)
        fail("invalid streaming view");
    if (original_) original_->setStreamingView(yaw, pitch, aspect);
}
bool World::chunkLoaded(int x, int z) const {
    requireLoaded();
    if (x < 0 || x >= width() || z < 0 || z >= depth()) return false;
    return !original_ || !streaming_ || original_->chunkLoaded(x, z);
}
StreamingInfo World::streamingInfo() const {
    requireLoaded();
    if (original_) return original_->streamingInfo();
    StreamingInfo info; info.windowSize = Width; info.maxDistance = 40; return info;
}
std::string World::streamingInfoJson() const {
    const auto i = streamingInfo();
    std::ostringstream out; out.imbue(std::locale::classic()); out << std::setprecision(8);
    out << "{\"enabled\":" << (i.enabled ? "true" : "false") << ",\"radius\":" << i.radius
        << ",\"preloadRadius\":" << i.preloadRadius
        << ",\"displayTarget\":" << i.displayTarget << ",\"displayLoaded\":" << i.displayLoaded
        << ",\"windowSize\":" << i.windowSize << ",\"maxDistance\":" << i.maxDistance
        << ",\"queued\":" << i.queued << ",\"loaded\":" << i.loaded << ",\"target\":" << i.target
        << ",\"resident\":" << i.resident << ",\"totalGenerated\":" << i.totalGenerated
        << ",\"centerChunkX\":" << i.centerChunkX << ",\"centerChunkZ\":" << i.centerChunkZ
        << ",\"baseCached\":" << i.baseCached << ",\"structureCached\":" << i.structureCached
        << ",\"deferred\":" << i.deferred << ",\"lastLoadMs\":" << i.lastLoadMs << '}';
    return out.str();
}
int World::width() const { requireLoaded(); return original_ ? original_->size() : Width; }
int World::height() const { requireLoaded(); return original_ ? OriginalWorld::Height : Height; }
int World::depth() const { requireLoaded(); return original_ ? original_->size() : Depth; }
bool World::originalGeneration() const { requireLoaded(); return bool(original_); }
const RenderVolume& World::renderVolume() const {
    requireLoaded();
    if (original_) return original_->renderVolume(x_, z_);
    renderVolume_ = {blocks_.data(), 0, 0, 0, Width, Height, Depth};
    return renderVolume_;
}
const uint16_t* World::blockData() const { return renderVolume().data; }
uint16_t World::blockAt(int x, int y, int z) const {
    requireLoaded();
    if (original_) {
        // [FIX] OriginalWorld::blockAt already refuses to generate for streaming
        // worlds: it looks up the resident FINAL cache and returns 0 when the
        // column is not committed. No side effects here.
        return original_->blockAt(x, y, z);
    }
    if (x < 0 || x >= Width || y < 0 || y >= Height || z < 0 || z >= Depth) return 0;
    return blocks_[cell(x, y, z)];
}

int World::biomeAt(int x, int z) const {
    requireLoaded();
    if (x < 0 || x >= width() || z < 0 || z >= depth()) fail("biome outside finite world");
    return original_ ? int(original_->biomeAt(x, z)) : -1;
}
uint32_t World::grassColorAt(int x, int z) const {
    const int biome = biomeAt(x, z);
    return biome < 0 ? 0x7cbd6bu : OriginalWorld::biomeMeta(biome).grassColor;
}
uint32_t World::foliageColorAt(int x, int z) const {
    const int biome = biomeAt(x, z);
    return biome < 0 ? 0x5bab46u : OriginalWorld::biomeMeta(biome).foliageColor;
}
bool World::structuresEnabled() const { requireLoaded(); return original_ && original_->structuresEnabled(); }
bool World::decorationsEnabled() const { requireLoaded(); return original_ && original_->decorationsEnabled(); }
bool World::carversEnabled() const { requireLoaded(); return original_ && original_->carversEnabled(); }
std::string World::biomeInfo() const {
    const int biome = biomeAt(int(std::floor(x_)), int(std::floor(z_)));
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "{\"id\":" << biome;
    if (biome < 0) {
        out << ",\"name\":" << quote("旧版草地（群系未知）")
            << ",\"sourceName\":\"grassland/unknown\",\"source\":\"legacy-custom-v1-no-biome-data\",\"known\":false"
            << ",\"grassColor\":" << 0x7cbd6bu << ",\"foliageColor\":" << 0x5bab46u;
    } else {
        const auto& meta = OriginalWorld::biomeMeta(biome);
        out << ",\"name\":" << quote(meta.name) << ",\"sourceName\":" << quote(meta.sourceName)
            << ",\"source\":\"original-biomes-Layer-Voronoi\",\"known\":true"
            << ",\"grassColor\":" << meta.grassColor << ",\"foliageColor\":" << meta.foliageColor;
    }
    out << '}';
    return out.str();
}
std::string World::landmarks() const {
    requireLoaded();
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << '[';
    if (structuresEnabled()) {
        auto found = original_->landmarks(int(x_), int(z_), std::min(256, width()));
        const auto distanceSquared = [this](const OriginalWorld::Landmark& v) {
            const double dx = v.x - double(x_), dz = v.z - double(z_); return dx * dx + dz * dz;
        };
        std::sort(found.begin(), found.end(), [&](const auto& a, const auto& b) {
            const double da = distanceSquared(a), db = distanceSquared(b);
            return da != db ? da < db : a.id < b.id;
        });
        for (size_t i = 0; i < found.size(); ++i) {
            const auto& v = found[i];
            if (i) out << ',';
            out << "{\"id\":" << v.id << ",\"type\":" << quote(v.type)
                << ",\"x\":" << v.x << ",\"z\":" << v.z << ",\"biome\":" << v.biome
                << ",\"distance\":" << std::sqrt(distanceSquared(v))
                << ",\"bounds\":[" << v.minX << ',' << v.minZ << ',' << v.maxX << ',' << v.maxZ
                << "],\"pieceCount\":" << v.pieceCount << '}';
        }
    }
    out << ']'; return out.str();
}

BlockHit World::pick(float yawDegrees, float pitchDegrees, float reach) const {
    return pickImpl(yawDegrees, pitchDegrees, reach, false);
}
BlockHit World::pickImpl(float yawDegrees, float pitchDegrees, float reach, bool includeWater) const {
    requireLoaded();
    if (!std::isfinite(yawDegrees) || !std::isfinite(pitchDegrees) || !std::isfinite(reach) || reach <= 0)
        fail("invalid interaction input");
    constexpr double Radians = .017453292519943295769;
    const double yaw = std::remainder(double(yawDegrees), 360.0) * Radians;
    const double pitch = std::remainder(double(pitchDegrees), 360.0) * Radians;
    const std::array<double, 3> origin {{double(x_), double(y_) + 1.62, double(z_)}};
    std::array<double, 3> direction {{std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)}};
    for (double& component : direction) if (std::abs(component) < 1e-12) component = 0;
    std::array<int, 3> voxel {}, step {};
    std::array<double, 3> next {}, delta {};
    for (size_t axis = 0; axis < 3; ++axis) {
        voxel[axis] = int(std::floor(origin[axis]));
        step[axis] = direction[axis] > 0 ? 1 : direction[axis] < 0 ? -1 : 0;
        if (step[axis] == 0) {
            next[axis] = delta[axis] = std::numeric_limits<double>::infinity();
        } else {
            delta[axis] = 1 / std::abs(direction[axis]);
            const double boundary = voxel[axis] + (step[axis] > 0 ? 1 : 0);
            next[axis] = (boundary - origin[axis]) / direction[axis];
        }
    }
    const double limit = std::min(double(reach), 5.0);
    double entered = 0;
    int face = -1;
    for (;;) {
        if (voxel[0] < 0 || voxel[0] >= width() || voxel[1] < 0 || voxel[1] >= height() || voxel[2] < 0 || voxel[2] >= depth())
            return {};
        // [FIX] Leaving the world is "unknown" (no resident column), not
        // "no hit". A streaming world may not have this column committed yet;
        // survival mining must keep the target alive instead of cancelling.
        if (!chunkLoaded(voxel[0], voxel[2])) {
            BlockHit boundary;
            boundary.hit = true;
            boundary.unknown = true;
            boundary.x = voxel[0]; boundary.y = voxel[1]; boundary.z = voxel[2];
            boundary.face = face;
            boundary.block = 0;
            boundary.distance = float(entered);
            return boundary;
        }
        const auto block = blockAt(voxel[0], voxel[1], voxel[2]);
        const bool water = includeWater && block &&
            (!std::strcmp(legacy::kBlockSpecs[block-1].key,"calmWater") || !std::strcmp(legacy::kBlockSpecs[block-1].key,"water"));
        if (solid(voxel[0], voxel[1], voxel[2]) || water)
            return {true, false, voxel[0], voxel[1], voxel[2], face, block, float(entered)};
        const double distance = std::min({next[0], next[1], next[2]});
        if (distance > limit) return {};
        bool choseFace = false;
        for (size_t axis = 0; axis < 3; ++axis) {
            if (next[axis] <= distance + 1e-12) {
                if (!choseFace) {
                    face = axis == 0 ? (step[axis] > 0 ? 4 : 5) : axis == 1 ? (step[axis] > 0 ? 0 : 1) : (step[axis] > 0 ? 2 : 3);
                    choseFace = true;
                }
                voxel[axis] += step[axis];
                next[axis] += delta[axis];
            }
        }
        entered = distance;
    }
}

bool World::mine(float yawDegrees, float pitchDegrees, float reach) {
    lastMinedBlock_ = -1;
    const BlockHit hit = pick(yawDegrees, pitchDegrees, reach);
    // [FIX] `unknown` means the ray left the resident window; there is no
    // concrete block to remove. `block == 0` covers the same case for safety.
    if (!hit.hit || hit.unknown || !breakable(hit.block)) return false;
    if (original_) original_->setBlock(hit.x, hit.y, hit.z, 0);
    else {
        const uint32_t index = uint32_t(cell(hit.x, hit.y, hit.z));
        edits_.insert(std::lower_bound(edits_.begin(), edits_.end(), index), index);
        blocks_[index] = 0; --blockCount_;
    }
    lastMinedBlock_ = int(hit.block) - 1;
    revision_ = nextRevision(); dirty_ = terrainDirty_ = true;
    lastEdit_ = {hit.x, hit.y, hit.z, revision_};
    return true;
}

bool World::place(float yawDegrees, float pitchDegrees, int catalogIndex, float reach) {
    requireLoaded();
    if (catalogIndex < 0 || catalogIndex >= legacy::kBlockCount) return false;
    const auto& spec = legacy::kBlockSpecs[catalogIndex];
    if (std::strcmp(spec.shape, "air") == 0 || std::strcmp(spec.shape, "liquid") == 0) return false;
    const BlockHit hit = pick(yawDegrees, pitchDegrees, reach);
    if (!original_) fail("placement unsupported for immutable custom-v1 removal-only saves");
    // [FIX] `unknown` has no concrete face/block; refuse placement rather than
    // indexing the catalog with block==0.
    if (!hit.hit || hit.unknown || hit.face < 0 || hit.face > 5) return false;
    static constexpr int offsets[6][3] = {{0,-1,0}, {0,1,0}, {0,0,-1}, {0,0,1}, {-1,0,0}, {1,0,0}};
    const int x = hit.x + offsets[hit.face][0], y = hit.y + offsets[hit.face][1], z = hit.z + offsets[hit.face][2];
    if (x < 0 || x >= width() || y <= 0 || y >= height() || z < 0 || z >= depth() || !chunkLoaded(x, z) || solid(x, y, z)) return false;
    if (x + 1 > x_ - Radius + Epsilon && x < x_ + Radius - Epsilon &&
        y + 1 > y_ + Epsilon && y < y_ + BodyHeight - Epsilon &&
        z + 1 > z_ - Radius + Epsilon && z < z_ + Radius - Epsilon) return false;
    if (std::find(originalPalette_.begin(), originalPalette_.end(), spec.key) == originalPalette_.end())
        fail("selected block key absent from immutable saved palette");
    if (!original_->setBlock(x, y, z, uint16_t(catalogIndex + 1))) return false;
    revision_ = nextRevision(); dirty_ = terrainDirty_ = true;
    lastEdit_ = {x, y, z, revision_};
    return true;
}

bool World::placeItem(float yawDegrees, float pitchDegrees, int sourceItemId, int aux, float reach) {
    requireLoaded();
    const auto* item = placementItem(sourceItemId, aux);
    if (!item || !item->placeable) return false;
    const auto hit = pickImpl(yawDegrees, pitchDegrees, reach, item->tileId == 111);
    if (!original_) fail("placement unsupported for immutable custom-v1 removal-only saves");
    if (!hit.hit || hit.unknown || hit.face < 0) return false;
    constexpr double Radians = .017453292519943295769;
    const double pitch = std::remainder(double(pitchDegrees), 360.0) * Radians;
    const double hitY = double(y_) + 1.62 + std::sin(pitch) * double(hit.distance) - hit.y;
    PlacementContext context;
    context.hit = hit; context.yaw = yawDegrees;
    context.clickHeight = hit.face == 0 ? 0 : hit.face == 1 ? 1 : float(std::clamp(hitY, 0.0, 1.0));
    context.playerX = x_; context.playerY = y_; context.playerZ = z_;
    context.blockAt = [this](int x,int y,int z) { return blockAt(x,y,z); };
    context.available = [this](int x,int y,int z) {
        return x >= 0 && x < width() && y > 0 && y < height() && z >= 0 && z < depth() && chunkLoaded(x,z);
    };
    std::vector<PlacementChange> planned;
    if (!planItemPlacement(*item, context, planned) || planned.empty()) return false;
    std::vector<OriginalWorld::BlockChange> changes;
    changes.reserve(planned.size());
    for (const auto& change : planned) {
        if (!context.available(change.x,change.y,change.z) || change.renderIndex < 0 || change.renderIndex >= legacy::kBlockCount) return false;
        const auto& spec = legacy::kBlockSpecs[change.renderIndex];
        if (!std::strcmp(spec.shape,"air") || !std::strcmp(spec.shape,"liquid")) return false;
        if (change.x+1 > x_-Radius+Epsilon && change.x < x_+Radius-Epsilon &&
            change.y+1 > y_+Epsilon && change.y < y_+BodyHeight-Epsilon &&
            change.z+1 > z_-Radius+Epsilon && change.z < z_+Radius-Epsilon) return false;
        if (std::find(originalPalette_.begin(),originalPalette_.end(),spec.key) == originalPalette_.end()) return false;
        changes.push_back({change.x,change.y,change.z,uint16_t(change.renderIndex+1)});
    }
    if (!original_->setBlocks(changes)) return false;
    revision_ = nextRevision(); dirty_ = terrainDirty_ = true;
    lastEdit_ = {changes.front().x, changes.front().y, changes.front().z, revision_};
    return true;
}

bool World::grounded() const {
    requireLoaded();
    if (grounded_) return true;
    // [FIX] If the cell directly under the feet is not resident, "standing on
    // it" is unknowable. `collides` would conservatively treat the missing
    // column as a wall and report grounded=true, which is wrong for survival
    // mining. Return false instead; movement re-resolves once the chunk streams.
    const int bx = int(std::floor(x_)), bz = int(std::floor(z_));
    if (original_ && streaming_ && !original_->chunkLoaded(bx, bz)) return false;
    return collides(x_, y_ - .002f, z_);
}
bool World::underWater() const {
    requireLoaded();
    const int x = int(std::floor(x_)), y = int(std::floor(y_ + 1.62f)), z = int(std::floor(z_));
    if (!chunkLoaded(x, z) || y < 0 || y >= height()) return false;
    const auto value = blockAt(x, y, z);
    return value && (legacy::kBlockSpecs[value - 1].tileId == 8 || legacy::kBlockSpecs[value - 1].tileId == 9);
}
bool World::till(float yaw, float pitch, float reach) {
    requireLoaded();
    if (!original_) return false;
    const auto hit = pick(yaw, pitch, reach);
    // [FIX] `unknown` has no concrete block; refuse rather than indexing -1.
    if (!hit.hit || hit.unknown || hit.face <= 0 || hit.y + 1 >= height() || !chunkLoaded(hit.x, hit.z)) return false;
    const int tile = legacy::kBlockSpecs[hit.block - 1].tileId;
    if ((tile != 2 && tile != 3) || blockAt(hit.x, hit.y + 1, hit.z) != 0) return false;
    const int farmland = placementState(60, 0);
    if (farmland < 0) return false;
    if (std::find(originalPalette_.begin(), originalPalette_.end(), legacy::kBlockSpecs[farmland].key) == originalPalette_.end()) return false;
    if (!original_->setBlock(hit.x, hit.y, hit.z, uint16_t(farmland + 1))) return false;
    revision_ = nextRevision(); dirty_ = terrainDirty_ = true;
    lastEdit_ = {hit.x, hit.y, hit.z, revision_};
    return true;
}

bool World::useBlock(float yaw, float pitch, float reach) {
    requireLoaded();
    if (!original_) fail("interaction unsupported for immutable custom-v1 removal-only saves");
    const BlockHit hit = pick(yaw, pitch, reach);
    // [FIX] `unknown` is not a concrete block; refuse.
    if (!hit.hit || hit.unknown) return false;
    InteractionContext context;
    context.hit = hit;
    context.sneaking = false; context.holdingItem = false;
    context.blockAt = [this](int x, int y, int z) { return blockAt(x, y, z); };
    context.available = [this](int x, int y, int z) {
        return x >= 0 && x < width() && y > 0 && y < height() && z >= 0 && z < depth() && chunkLoaded(x, z);
    };
    std::vector<InteractionChange> planned;
    const auto result = planBlockInteraction(context, planned);
    if (result == InteractionResult::Unhandled) return false;
    if (result == InteractionResult::Consumed) return true;
    if (planned.empty()) return false;
    std::vector<OriginalWorld::BlockChange> changes;
    changes.reserve(planned.size());
    for (const auto& change : planned) {
        if (!context.available(change.x, change.y, change.z) ||
            change.renderIndex < 0 || change.renderIndex >= legacy::kBlockCount) return false;
        const auto& spec = legacy::kBlockSpecs[change.renderIndex];
        if (!std::strcmp(spec.shape, "air") || !std::strcmp(spec.shape, "liquid")) return false;
        if (std::find(originalPalette_.begin(), originalPalette_.end(), spec.key) == originalPalette_.end()) return false;
        changes.push_back({change.x, change.y, change.z, uint16_t(change.renderIndex + 1)});
    }
    if (!original_->setBlocks(changes)) return false;
    revision_ = nextRevision(); dirty_ = terrainDirty_ = true;
    lastEdit_ = {changes.front().x, changes.front().y, changes.front().z, revision_};
    return true;
}

void World::generate() {
    const uint16_t grass = id("grass"), dirt = id("dirt"), stone = id("stone"), bedrock = id("unbreakable");
    const uint16_t water = id("calmWater"), sand = id("sand"), log = id("log_oak"), leaves = id("leaves_oak");
    const uint16_t coal = id("coalOre"), iron = id("ironOre"), gold = id("goldOre"), diamond = id("diamondOre");
    blocks_.assign(Cells, 0);
    std::array<int, Width * Depth> heights {};
    constexpr int sea = 18;
    const int pondX = 13 + int(mix(seed_ + 1) % 7), pondZ = 16 + int(mix(seed_ + 2) % 10);
    for (int z = 0; z < Depth; ++z) for (int x = 0; x < Width; ++x) {
        int height = 23 + noise(seed_, x, z, 24) * 8 / 32768 + noise(seed_ ^ 71, x, z, 9) * 3 / 32768;
        const int distance = (x - pondX) * (x - pondX) + (z - pondZ) * (z - pondZ);
        if (distance < 64) height = std::min(height, 15 + distance / 20);
        if (std::abs(x - 32) <= 4 && std::abs(z - 32) <= 4) height = 23;
        height = std::clamp(height, 10, 34);
        heights[size_t(z) * Width + x] = height;
        for (int y = 0; y <= std::max(height, sea); ++y) {
            uint16_t block = 0;
            if (y == 0) block = bedrock;
            else if (y > height) block = water;
            else if (y == height) block = height <= sea ? sand : grass;
            else if (y >= height - 3) block = height <= sea ? sand : dirt;
            else {
                block = stone;
                uint32_t ore = mix(seed_ ^ uint32_t(x / 2) * 733u ^ uint32_t(y / 2) * 19213u ^ uint32_t(z / 2) * 199933u) % 1000;
                if (ore < 32) block = coal;
                else if (ore < 49) block = iron;
                else if (ore < 54 && y < 12) block = gold;
                else if (ore < 56 && y < 8) block = diamond;
            }
            blocks_[cell(x, y, z)] = block;
        }
    }
    for (int z = 4; z < Depth - 4; z += 4) for (int x = 4; x < Width - 4; x += 4) {
        if (std::abs(x - 32) <= 7 && std::abs(z - 32) <= 7) continue;
        const uint32_t random = mix(seed_ ^ uint32_t(x) * 9239u ^ uint32_t(z) * 12143u);
        if (random % 4 != 0) continue;
        int ground = heights[size_t(z) * Width + x];
        if (ground <= sea || blocks_[cell(x, ground, z)] != grass) continue;
        bool clear = true;
        for (int dz = -2; dz <= 2; ++dz) for (int dx = -2; dx <= 2; ++dx)
            for (int y = ground + 1; y <= ground + 7; ++y)
                if (blocks_[cell(x + dx, y, z + dz)] != 0) clear = false;
        if (!clear) continue;
        int top = ground + 4 + int(random % 2);
        for (int y = ground + 1; y <= top; ++y) blocks_[cell(x, y, z)] = log;
        for (int y = top - 2; y <= top + 1; ++y) {
            int radius = y == top + 1 ? 1 : 2;
            for (int dz = -radius; dz <= radius; ++dz) for (int dx = -radius; dx <= radius; ++dx) {
                if (std::abs(dx) == radius && std::abs(dz) == radius) continue;
                auto& block = blocks_[cell(x + dx, y, z + dz)];
                if (block == 0) block = leaves;
            }
        }
    }
    x_ = 32.5f; y_ = 24; z_ = 32.5f; velocityY_ = 0; grounded_ = true;
    blockCount_ = int(std::count_if(blocks_.begin(), blocks_.end(), [](uint16_t v) { return v != 0; }));
}

void World::spawnOriginal(const ProgressCallback& progress) {
    const int center = original_->size() / 2;
    const uint16_t grass = id("grass");
    for (int preference = 0; preference < 2; ++preference) {
        for (int radius = 0; radius <= 128; ++radius) {
            for (int dz = -radius; dz <= radius; ++dz) for (int dx = -radius; dx <= radius; ++dx) {
                if (radius && std::abs(dx) != radius && std::abs(dz) != radius) continue;
                const int x = center + dx, z = center + dz;
                if (original_->loadChunk(x, z) && progress)
                    progress({"spawn", int(original_->streamingInfo().totalGenerated), 0});
                for (int y = 127; y > 0; --y) {
                    const uint16_t block = original_->blockAt(x, y, z);
                    if (!block) continue;
                    if (std::strcmp(legacy::kBlockSpecs[block - 1].shape, "liquid") == 0) break;
                    if ((preference == 0 && block != grass) || !solid(x, y, z)) break;
                    if (original_->blockAt(x, y + 1, z) || original_->blockAt(x, y + 2, z)) break;
                    x_ = x + .5f; y_ = float(y + 1); z_ = z + .5f;
                    velocityY_ = 0; grounded_ = true; return;
                }
            }
        }
    }
    fail("no safe dry spawn near original world center");
}

void World::create(uint32_t seed, const std::string& root, bool original, int worldSize, bool structures, bool decoration, ProgressCallback progress) {
    // New decorated worlds are v4 (carvers + ores); v1..v3 remain selectable.
    createGeneration(seed, root, original, worldSize, structures, decoration, decoration, std::move(progress));
}

void World::createGeneration(uint32_t seed, const std::string& root, bool original, int worldSize, bool structures,
                             bool decoration, bool carvers, ProgressCallback progress) {
    if (carvers && !decoration) fail("carver generation v4 requires decoration");
    Directory parent = directory(root, true);
    World candidate;
    candidate.streaming_ = streaming_; candidate.renderRadius_ = renderRadius_; candidate.preloadRadius_ = preloadRadius_;
    if (progress) progress({"spawn", 0, 0});
    candidate.root_ = parent.path;
    candidate.rootDevice_ = uint64_t(parent.info.st_dev); candidate.rootInode_ = uint64_t(parent.info.st_ino);
    candidate.seed_ = seed;
    if (original) {
        candidate.original_ = std::make_shared<OriginalWorld>(seed, worldSize, structures, decoration, carvers);
        candidate.configureOriginal(); candidate.spawnOriginal(progress);
        if (candidate.streaming_) candidate.original_->setStreamingCenter(candidate.x_, candidate.z_);
        candidate.loadPlayerSafety();
    } else candidate.generate();
    if (progress) {
        const int count = original ? int(candidate.original_->streamingInfo().totalGenerated) : 1;
        progress({"spawn", count, count});
    }
    const auto stamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::string name;
    for (int attempt = 0; attempt < 128; ++attempt) {
        name = "world-" + std::to_string(stamp + attempt) + "-" + std::to_string(seed);
        if (::mkdirat(parent.fd.value, name.c_str(), 0700) == 0) break;
        if (errno != EEXIST) systemFail("create unique world directory");
        if (attempt == 127) fail("cannot allocate unique world directory");
    }
    candidate.path_ = parent.path + "/" + name;
    Directory world = directory(candidate.path_);
    candidate.worldDevice_ = uint64_t(world.info.st_dev); candidate.worldInode_ = uint64_t(world.info.st_ino);
    candidate.loaded_ = true;
    World previous;
    bool published = false;
    try {
        if (progress) progress({"initialsave", 0, 1});
        candidate.save();
        if (::fsync(parent.fd.value) != 0) systemFail("flush root directory");
        if (progress) progress({"initialsave", 1, 1});
        candidate.revision_ = nextRevision();
        previous = std::move(*this);
        *this = std::move(candidate); published = true;
        if (progress) { progress({"post", 0, 1}); progress({"post", 1, 1}); }
    } catch (...) {
        if (published) *this = std::move(previous);
        struct stat named {};
        if (!::fstatat(parent.fd.value, name.c_str(), &named, AT_SYMLINK_NOFOLLOW) &&
            S_ISDIR(named.st_mode) && named.st_dev == world.info.st_dev && named.st_ino == world.info.st_ino) {
            for (const char* file : {"level.json", "chunks.bin", "state.bin"}) ::unlinkat(world.fd.value, file, 0);
            ::unlinkat(parent.fd.value, name.c_str(), AT_REMOVEDIR);
        }
        throw;
    }
}

void World::save() {
    requireLoaded();
    if (original_) { saveOriginal(); return; }
    Directory parent = directory(root_);
    identity(parent.info, rootDevice_, rootInode_);
    Directory world = directory(path_);
    if (!beneath(world.path, parent.path)) fail("save path is outside root");
    identity(world.info, worldDevice_, worldInode_);
    if (!persisted_) {
        const auto keys = catalog();
        paletteHash_ = paletteHash(keys);
        Bytes chunks(std::begin(ChunkMagic), std::end(ChunkMagic));
        put32(chunks, Version); put32(chunks, Width); put32(chunks, Height); put32(chunks, Depth);
        put32(chunks, seed_); put32(chunks, uint32_t(keys.size())); put32(chunks, uint32_t(Cells)); put32(chunks, paletteHash_);
        for (uint16_t block : blocks_) { chunks.push_back(static_cast<unsigned char>(block)); chunks.push_back(static_cast<unsigned char>(block >> 8)); }
        chunkHash_ = hash(chunks);
        std::ostringstream level;
        level.imbue(std::locale::classic());
        level << "{\n  \"format\": \"" << Format << "\",\n  \"version\": " << Version
              << ",\n  \"generator\": \"native-value-noise-v1-not-original-minecraft\",\n  \"seed\": " << seed_
              << ",\n  \"size\": [64,48,64],\n  \"storage\": \"uint16-le-yzx-palette-index-plus-one\",\n  \"paletteHash\": " << paletteHash_
              << ",\n  \"chunksHash\": " << chunkHash_ << ",\n  \"palette\": [";
        for (size_t i = 0; i < keys.size(); ++i) { if (i) level << ','; level << quote(keys[i]); }
        level << "]\n}\n";
        const std::string metadata = level.str();
        atomicWrite(world.fd.value, "chunks.bin", chunks, false);
        atomicWrite(world.fd.value, "level.json", Bytes(metadata.begin(), metadata.end()), false);
    } else {
        regularTarget(world.fd.value, "chunks.bin", false);
        regularTarget(world.fd.value, "level.json", false);
    }
    if (readEdits(world.fd.value, chunkHash_) != persistedEdits_) fail("saved edits changed externally");
    regularTarget(world.fd.value, "state.bin", !persisted_);
    if (terrainDirty_) {
        auto committed = edits_;
        atomicWrite(world.fd.value, "edits.bin", encodeEdits(chunkHash_, edits_), true);
        persistedEdits_.swap(committed);
        terrainDirty_ = false;
    }
    Bytes state(std::begin(StateMagic), std::end(StateMagic));
    put32(state, Version); put32(state, seed_); put32(state, chunkHash_); put32(state, paletteHash_);
    putFloat(state, x_); putFloat(state, y_); putFloat(state, z_); putFloat(state, velocityY_);
    put32(state, hash(state));
    atomicWrite(world.fd.value, "state.bin", state, persisted_);
    persisted_ = true; dirty_ = false; saveTime_ = 0;
}

Bytes World::encodeOriginalMetadata() const {
    std::ostringstream level;
    level.imbue(std::locale::classic());
    const int size = original_->size();
    level << "{\n\"format\":" << quote(OriginalFormat) << ",\"version\":1,\"generator\":"
          << quote(generatorName(*original_))
          << ",\"seed\":" << seed_ << ",\"size\":[" << size << ",256," << size
          << "],\"generationDepth\":128,\"seaLevel\":63,\"biomeCount\":24,\"sourceCommit\":" << quote(SourceCommit)
          << ",\"sourceOrigin\":" << quote("-" + std::to_string(size / 2) + ",0,-" + std::to_string(size / 2))
          << ",\"limitations\":" << quote(generatorLimitations(*original_));
    if (original_->decorationsEnabled()) level << ",\"structures\":" << (original_->structuresEnabled() ? 1 : 0);
    level << ",\"storage\":\"modified-chunks-uint16-le-yzx-palette-plus-one\""
          << ",\"paletteHash\":" << paletteHash_ << ",\"palette\":[";
    for (size_t i = 0; i < originalPalette_.size(); ++i) { if (i) level << ','; level << quote(originalPalette_[i]); }
    level << "]\n}\n";
    const auto text = level.str();
    return Bytes(text.begin(), text.end());
}

void World::saveOriginal() {
    Directory parent = directory(root_);
    identity(parent.info, rootDevice_, rootInode_);
    Directory world = directory(path_);
    if (!beneath(world.path, parent.path)) fail("save path outside root");
    identity(world.info, worldDevice_, worldInode_);
    if (!persisted_) {
        originalPalette_ = catalog(); paletteHash_ = paletteHash(originalPalette_);
        const Bytes metadata = encodeOriginalMetadata();
        originalMetadataHash_ = hash(metadata);
        atomicWrite(world.fd.value, "level.json", metadata, false, true);
    } else {
        if (hash(readFile(world.fd.value, "level.json", MaxMetadata)) != originalMetadataHash_) fail("original metadata changed externally");
    }
    const auto files = originalChunkFiles(world.fd.value, original_->size());
    size_t expected = 0, editCount = 0;
    for (const auto& entry : original_->modifiedChunks()) {
        editCount += entry.second.edits.size();
        if (!entry.second.persisted) continue;
        ++expected;
        const auto it = files.find(entry.first);
        if (it == files.end() || hash(it->second) != entry.second.persistedHash) fail("original saved chunk changed externally");
    }
    if (files.size() != expected) fail("unexpected original saved chunk");
    if (editCount > MaxOriginalEdits) fail("original edit limit exceeded");
    const auto stores = dimensionDirectories(world.fd.value);
    std::set<std::string> declared;
    for (int slot : dimensions_) {
        const std::string name = dimensionDirectoryName(slot);
        if (!name.empty()) declared.insert(name);
    }
    if (saveVersion_ != SaveVersion::V2 && !stores.empty()) fail("dimension store in a v1 save");
    for (const auto& name : stores) if (!declared.count(name)) fail("unexpected dimension store: " + name);
    if (saveVersion_ == SaveVersion::V2) {
        regularTarget(world.fd.value, "dimensions.bin", true);
        const Bytes manifest = encodeDimensions(originalMetadataHash_, dimensions_);
        if (readOptionalFile(world.fd.value, "dimensions.bin", MaxDimensionFile) != manifest)
            atomicWrite(world.fd.value, "dimensions.bin", manifest, true, true);
    }
    regularTarget(world.fd.value, "state.bin", !persisted_);
    const auto keys = catalog();
    std::map<std::string, uint16_t> saved;
    for (size_t i = 0; i < originalPalette_.size(); ++i) saved.emplace(originalPalette_[i], uint16_t(i + 1));
    std::vector<uint16_t> savedIds(keys.size() + 1, 0);
    for (size_t i = 0; i < keys.size(); ++i) {
        const auto it = saved.find(keys[i]); if (it != saved.end()) savedIds[i + 1] = it->second;
    }
    for (const auto& entry : original_->modifiedChunks()) {
        if (!entry.second.dirty) continue;
        const auto bytes = encodeOriginalChunk(entry.second, entry.first, seed_, originalMetadataHash_, savedIds);
        atomicWrite(world.fd.value, originalChunkName(entry.first), bytes, entry.second.persisted, true);
        original_->markPersisted(entry.first, hash(bytes));
    }
    Bytes state(std::begin(OriginalStateMagic), std::end(OriginalStateMagic));
    put32(state, Version); put32(state, seed_); put32(state, originalMetadataHash_); put32(state, paletteHash_);
    putFloat(state, x_); putFloat(state, y_); putFloat(state, z_); putFloat(state, velocityY_);
    if (saveVersion_ == SaveVersion::V2) put32(state, uint32_t(currentDimension_));
    put32(state, hash(state));
    atomicWrite(world.fd.value, "state.bin", state, persisted_, true);
    persisted_ = true; dirty_ = terrainDirty_ = false; saveTime_ = 0;
}

void World::open(const std::string& path, const std::string& root) {
    Directory parent = directory(root);
    Directory world = directory(path.empty() || path[0] == '/' ? path : parent.path + "/" + path);
    if (!beneath(world.path, parent.path)) fail("world path is outside root");
    const Bytes metadata = readFile(world.fd.value, "level.json", MaxMetadata);
    const Json level = JsonParser(std::string(metadata.begin(), metadata.end())).parse();
    if (field(level, "format", Json::String).text == OriginalFormat) {
        const auto& generator = field(level, "generator", Json::String).text;
        const bool carvers = generator == CarversGenerator;
        const bool decoration = carvers || generator == DecorationGenerator;
        const bool structures = decoration ? integer(level, "structures") == 1 : generator == StructuresGenerator;
        const size_t fields = OriginalV1Fields + (decoration ? 1 : 0);
        if (level.object.size() != fields || integer(level, "version") != Version ||
            (decoration && integer(level, "structures") > 1) ||
            (generator != OriginalGenerator && generator != StructuresGenerator && !decoration) ||
            integer(level, "generationDepth") != 128 || integer(level, "seaLevel") != 63 || integer(level, "biomeCount") != 24 ||
            field(level, "sourceCommit", Json::String).text != SourceCommit ||
            field(level, "limitations", Json::String).text != (carvers ? CarversLimitations : decoration ? DecorationLimitations : structures ? StructuresLimitations : TerrainLimitations) ||
            field(level, "storage", Json::String).text != "modified-chunks-uint16-le-yzx-palette-plus-one")
            fail("unsupported original save metadata");
        const auto& dimensions = field(level, "size", Json::Array).array;
        if (dimensions.size() != 3 || !OriginalWorld::validSize(int(integer(dimensions[0]))) ||
            integer(dimensions[1]) != 256 || integer(dimensions[2]) != integer(dimensions[0])) fail("invalid original dimensions");
        const int size = int(integer(dimensions[0]));
        if (field(level, "sourceOrigin", Json::String).text != "-" + std::to_string(size / 2) + ",0,-" + std::to_string(size / 2))
            fail("invalid source coordinate origin");
        World candidate;
        candidate.streaming_ = streaming_; candidate.renderRadius_ = renderRadius_; candidate.preloadRadius_ = preloadRadius_;
        candidate.seed_ = integer(level, "seed"); candidate.paletteHash_ = integer(level, "paletteHash");
        candidate.originalMetadataHash_ = hash(metadata);
        const Bytes manifest = readOptionalFile(world.fd.value, "dimensions.bin", MaxDimensionFile);
        const bool v2 = !manifest.empty();
        candidate.saveVersion_ = v2 ? SaveVersion::V2 : SaveVersion::V1;
        candidate.dimensions_.clear();
        if (v2) candidate.dimensions_ = decodeDimensions(manifest, candidate.originalMetadataHash_);
        else candidate.dimensions_.push_back(OverworldSlot);
        {
            std::set<std::string> declared;
            for (int slot : candidate.dimensions_) {
                const std::string name = dimensionDirectoryName(slot);
                if (!name.empty()) declared.insert(name);
            }
            for (const auto& name : dimensionDirectories(world.fd.value))
                if (!v2 || !declared.count(name)) fail("unexpected dimension store: " + name);
        }
        const auto current = catalog();
        std::map<std::string, uint16_t> mapping;
        for (size_t i = 0; i < current.size(); ++i) mapping.emplace(current[i], uint16_t(i + 1));
        std::vector<uint16_t> remap(1, 0);
        std::set<std::string> unique;
        const auto& palette = field(level, "palette", Json::Array).array;
        if (palette.empty() || palette.size() > 65535) fail("invalid original palette");
        for (const auto& key : palette) {
            if (key.kind != Json::String || key.text.empty() || !unique.insert(key.text).second) fail("duplicate/invalid original palette key");
            const auto it = mapping.find(key.text);
            if (it == mapping.end()) fail("unknown original palette key");
            candidate.originalPalette_.push_back(key.text); remap.push_back(it->second);
        }
        if (paletteHash(candidate.originalPalette_) != candidate.paletteHash_) fail("original palette checksum mismatch");
        candidate.original_ = std::make_shared<OriginalWorld>(candidate.seed_, size, structures, decoration, carvers);
        candidate.configureOriginal();
        for (const auto& file : originalChunkFiles(world.fd.value, size))
            candidate.original_->restoreChunk(file.first, decodeOriginalChunk(file.second, file.first, candidate.seed_, candidate.originalMetadataHash_, remap), true);
        const size_t stateSize = candidate.saveVersion_ == SaveVersion::V2 ? 48 : 44;
        const auto state = readFile(world.fd.value, "state.bin", stateSize);
        if (state.size() != stateSize) fail("invalid original state length");
        Reader player {state}; player.magic(OriginalStateMagic);
        if (player.u32() != Version || player.u32() != candidate.seed_ || player.u32() != candidate.originalMetadataHash_ || player.u32() != candidate.paletteHash_)
            fail("invalid original state header");
        candidate.x_ = player.number(); candidate.y_ = player.number(); candidate.z_ = player.number(); candidate.velocityY_ = player.number();
        candidate.currentDimension_ = OverworldSlot;
        if (candidate.saveVersion_ == SaveVersion::V2) {
            const uint32_t slot = player.u32();
            if (slot > uint32_t(MaxDimensionSlot) ||
                !std::count(candidate.dimensions_.begin(), candidate.dimensions_.end(), int(slot)))
                fail("current dimension is not in the manifest");
            candidate.currentDimension_ = int(slot);
        }
        if (player.u32() != hash(state.data(), state.size() - 4) || std::abs(candidate.velocityY_) > 50 ||
            candidate.x_ < Radius || candidate.x_ > size - Radius || candidate.z_ < Radius || candidate.z_ > size - Radius ||
            candidate.y_ < 0 || candidate.y_ + BodyHeight > OriginalWorld::Height) fail("invalid original player state");
        if (candidate.streaming_) candidate.original_->setStreamingCenter(candidate.x_, candidate.z_);
        candidate.loadPlayerSafety();
        if (candidate.collides(candidate.x_, candidate.y_, candidate.z_)) fail("invalid original player state");
        candidate.root_ = parent.path; candidate.path_ = world.path;
        candidate.rootDevice_ = uint64_t(parent.info.st_dev); candidate.rootInode_ = uint64_t(parent.info.st_ino);
        candidate.worldDevice_ = uint64_t(world.info.st_dev); candidate.worldInode_ = uint64_t(world.info.st_ino);
        candidate.grounded_ = candidate.collides(candidate.x_, candidate.y_ - .002f, candidate.z_);
        candidate.loaded_ = candidate.persisted_ = true; candidate.revision_ = nextRevision();
        *this = std::move(candidate); return;
    }
    if (level.kind != Json::Object || level.object.size() != 9 ||
        field(level, "format", Json::String).text != Format || integer(level, "version") != Version ||
        field(level, "generator", Json::String).text != "native-value-noise-v1-not-original-minecraft" ||
        field(level, "storage", Json::String).text != "uint16-le-yzx-palette-index-plus-one") fail("unsupported custom save format");
    const auto& size = field(level, "size", Json::Array).array;
    if (size.size() != 3 || integer(size[0]) != Width || integer(size[1]) != Height || integer(size[2]) != Depth)
        fail("invalid world dimensions");
    const auto current = catalog();
    std::map<std::string, uint16_t> mapping;
    for (size_t i = 0; i < current.size(); ++i) mapping.emplace(current[i], uint16_t(i + 1));
    const auto& palette = field(level, "palette", Json::Array).array;
    if (palette.empty() || palette.size() > 65535) fail("invalid saved palette size");
    std::vector<std::string> savedKeys;
    std::vector<uint16_t> remap(1, 0);
    std::set<std::string> unique;
    for (const auto& key : palette) {
        if (key.kind != Json::String || key.text.empty() || !unique.insert(key.text).second) fail("invalid or duplicate palette key");
        const auto it = mapping.find(key.text);
        if (it == mapping.end()) fail("unsupported saved catalog key: " + key.text);
        savedKeys.push_back(key.text); remap.push_back(it->second);
    }
    World candidate;
    candidate.streaming_ = streaming_; candidate.renderRadius_ = renderRadius_; candidate.preloadRadius_ = preloadRadius_;
    candidate.seed_ = integer(level, "seed");
    candidate.paletteHash_ = integer(level, "paletteHash");
    candidate.chunkHash_ = integer(level, "chunksHash");
    if (paletteHash(savedKeys) != candidate.paletteHash_) fail("palette checksum mismatch");
    const Bytes chunks = readFile(world.fd.value, "chunks.bin", 40 + Cells * 2);
    if (chunks.size() != 40 + Cells * 2 || hash(chunks) != candidate.chunkHash_) fail("truncated/corrupt chunks");
    Reader chunk {chunks}; chunk.magic(ChunkMagic);
    if (chunk.u32() != Version || chunk.u32() != Width || chunk.u32() != Height || chunk.u32() != Depth ||
        chunk.u32() != candidate.seed_ || chunk.u32() != palette.size() || chunk.u32() != Cells ||
        chunk.u32() != candidate.paletteHash_) fail("invalid chunk header");
    candidate.blocks_.resize(Cells);
    for (size_t i = 0; i < Cells; ++i) {
        uint16_t block = uint16_t(chunks[chunk.offset] | (uint16_t(chunks[chunk.offset + 1]) << 8)); chunk.offset += 2;
        if (block >= remap.size()) fail("invalid block palette id");
        candidate.blocks_[i] = remap[block];
        if (block) ++candidate.blockCount_;
    }
    candidate.edits_ = readEdits(world.fd.value, candidate.chunkHash_);
    for (uint32_t index : candidate.edits_) {
        if (!breakable(candidate.blocks_[index])) fail("edit does not remove a breakable solid");
        candidate.blocks_[index] = 0;
        --candidate.blockCount_;
    }
    candidate.persistedEdits_ = candidate.edits_;
    const Bytes state = readFile(world.fd.value, "state.bin", 44);
    if (state.size() != 44) fail("truncated/oversized player state");
    Reader player {state}; player.magic(StateMagic);
    if (player.u32() != Version || player.u32() != candidate.seed_ || player.u32() != candidate.chunkHash_ ||
        player.u32() != candidate.paletteHash_) fail("invalid state header");
    candidate.x_ = player.number(); candidate.y_ = player.number(); candidate.z_ = player.number(); candidate.velocityY_ = player.number();
    if (player.u32() != hash(state.data(), state.size() - 4)) fail("player checksum mismatch");
    if (candidate.x_ < Radius || candidate.x_ > Width - Radius || candidate.z_ < Radius || candidate.z_ > Depth - Radius ||
        candidate.y_ < 0 || candidate.y_ > Height - BodyHeight || std::abs(candidate.velocityY_) > 50 ||
        candidate.collides(candidate.x_, candidate.y_, candidate.z_)) fail("invalid or obstructed player position");
    candidate.root_ = parent.path; candidate.path_ = world.path;
    candidate.rootDevice_ = uint64_t(parent.info.st_dev); candidate.rootInode_ = uint64_t(parent.info.st_ino);
    candidate.worldDevice_ = uint64_t(world.info.st_dev); candidate.worldInode_ = uint64_t(world.info.st_ino);
    candidate.grounded_ = candidate.collides(candidate.x_, candidate.y_ - .002f, candidate.z_);
    candidate.loaded_ = true; candidate.persisted_ = true;
    candidate.revision_ = nextRevision();
    *this = std::move(candidate);
}

std::string World::dimensionDirectory(int slot) { return dimensionDirectoryName(slot); }
bool World::hasDimension(int slot) const {
    return std::count(dimensions_.begin(), dimensions_.end(), slot) != 0;
}
void World::upgradeToV2() {
    requireLoaded();
    if (!original_) fail("custom saves have no dimension container");
    if (saveVersion_ == SaveVersion::V2) return;
    if (!persisted_) fail("upgrade requires a saved world");
    Directory parent = directory(root_);
    identity(parent.info, rootDevice_, rootInode_);
    Directory world = directory(path_);
    if (!beneath(world.path, parent.path)) fail("save path outside root");
    identity(world.info, worldDevice_, worldInode_);
    if (hash(readFile(world.fd.value, "level.json", MaxMetadata)) != originalMetadataHash_)
        fail("original metadata changed externally");
    const auto previousVersion = saveVersion_;
    const auto previousDimensions = dimensions_;
    saveVersion_ = SaveVersion::V2;
    dimensions_ = {OverworldSlot};
    currentDimension_ = OverworldSlot;
    try {
        dirty_ = true;
        saveOriginal();
    } catch (...) {
        saveVersion_ = previousVersion;
        dimensions_ = previousDimensions; currentDimension_ = OverworldSlot;
        ::unlinkat(world.fd.value, "dimensions.bin", 0);
        throw;
    }
}
void World::addDimension(int slot) {
    requireLoaded();
    if (saveVersion_ != SaveVersion::V2) fail("dimension registration requires a v2 save");
    if (slot <= OverworldSlot || slot > MaxDimensionSlot) fail("invalid dimension slot");
    if (hasDimension(slot)) return;
    Directory parent = directory(root_);
    identity(parent.info, rootDevice_, rootInode_);
    Directory world = directory(path_);
    if (!beneath(world.path, parent.path)) fail("save path outside root");
    identity(world.info, worldDevice_, worldInode_);
    if (hash(readFile(world.fd.value, "level.json", MaxMetadata)) != originalMetadataHash_)
        fail("original metadata changed externally");
    const std::string name = dimensionDirectoryName(slot);
    if (::mkdirat(world.fd.value, name.c_str(), 0700) != 0 && errno != EEXIST)
        systemFail("create dimension store " + name);
    auto previousDimensions = dimensions_;
    dimensions_.push_back(slot);
    std::sort(dimensions_.begin(), dimensions_.end());
    try {
        dirty_ = true;
        saveOriginal();
    } catch (...) {
        dimensions_.swap(previousDimensions);
        ::unlinkat(world.fd.value, name.c_str(), AT_REMOVEDIR);
        throw;
    }
}
void World::setCurrentDimension(int slot) {
    requireLoaded();
    if (saveVersion_ != SaveVersion::V2) fail("dimension tracking requires a v2 save");
    if (!hasDimension(slot)) fail("dimension is not registered");
    if (currentDimension_ == slot) return;
    currentDimension_ = slot;
    dirty_ = true;
}

bool World::solid(int x, int y, int z) const {
    const int w = original_ ? original_->size() : Width, h = original_ ? OriginalWorld::Height : Height;
    if (x < 0 || x >= w || y < 0 || y >= h || z < 0 || z >= w) return true;
    if (original_ && streaming_ && !original_->chunkLoaded(x, z)) return true; // UNKNOWN is a physics wall
    const uint16_t block = original_ ? original_->blockAt(x, y, z) : blocks_[cell(x, y, z)];
    if (!block) return false;
    const auto& spec = legacy::kBlockSpecs[block - 1];
    return std::strcmp(spec.shape, "liquid") != 0 && std::strcmp(spec.shape, "air") != 0;
}
bool World::collides(float x, float y, float z) const {
    const int w = original_ ? original_->size() : Width, h = original_ ? OriginalWorld::Height : Height;
    if (x < Radius || x > w - Radius || z < Radius || z > w - Radius || y < 0 || y + BodyHeight > h) return true;
    const CollisionBox body {x-Radius+Epsilon,y+Epsilon,z-Radius+Epsilon,
                             x+Radius-Epsilon,y+BodyHeight-Epsilon,z+Radius-Epsilon};
    const auto available = [&](int bx,int by,int bz) {
        return bx>=0 && bx<w && by>=0 && by<h && bz>=0 && bz<w &&
               (!original_ || !streaming_ || original_->chunkLoaded(bx,bz));
    };
    const CollisionContext context {
        [&](int bx,int by,int bz) -> uint16_t { return original_ ? original_->blockAt(bx,by,bz) : blocks_[cell(bx,by,bz)]; },
        available
    };
    for (int by = std::max(0,int(std::floor(body.y0))-1); by <= int(std::floor(body.y1)); ++by)
        for (int bz = int(std::floor(body.z0)); bz <= int(std::floor(body.z1)); ++bz)
            for (int bx = int(std::floor(body.x0)); bx <= int(std::floor(body.x1)); ++bx) {
                if (!available(bx,by,bz)) {
                    if (collisionIntersects(body,{float(bx),float(by),float(bz),float(bx+1),float(by+1),float(bz+1)})) return true;
                    continue;
                }
                const auto boxes=blockCollisionBoxes(collisionState(context.blockAt(bx,by,bz)),bx,by,bz,context);
                for (int i=0;i<boxes.count;++i) if (collisionIntersects(body,boxes.boxes[size_t(i)])) return true;
            }
    return false;
}
bool World::onLadder() const {
    const int bx=int(std::floor(x_)), by=int(std::floor(y_+2*Epsilon)), bz=int(std::floor(z_));
    if (bx<0 || bx>=width() || by<0 || by>=height() || bz<0 || bz>=depth() ||
        (original_ && streaming_ && !original_->chunkLoaded(bx,bz))) return false;
    const int w = original_ ? original_->size() : Width, h = original_ ? OriginalWorld::Height : Height;
    const CollisionContext context {
        [&](int cx,int cy,int cz) -> uint16_t { return original_ ? original_->blockAt(cx,cy,cz) : blocks_[cell(cx,cy,cz)]; },
        [&](int cx,int cy,int cz) {
            return cx>=0 && cx<w && cy>=0 && cy<h && cz>=0 && cz<w &&
                   (!original_ || !streaming_ || original_->chunkLoaded(cx,cz));
        }
    };
    return collisionClimbable(collisionState(context.blockAt(bx,by,bz)), bx, by, bz, context);
}
void World::moveVertical(float distance) {
    const int steps=std::max(1,int(std::ceil(std::abs(distance)/.05f)));
    const float part=distance/float(steps);
    for (int n=0;n<steps;++n) {
        if (!collides(x_,y_+part,z_)) { y_+=part; continue; }
        float low=0, high=1;
        for (int i=0;i<15;++i) {
            const float middle=(low+high)*.5f;
            if (collides(x_,y_+part*middle,z_)) high=middle; else low=middle;
        }
        y_+=part*low;
        if (part<0) grounded_=true;
        velocityY_=0;
        return;
    }
}
void World::moveHorizontal(float dx, float dz) {
    auto axis = [&](float moveX, float moveZ) {
        if (moveX == 0 && moveZ == 0) return;
        if (!collides(x_ + moveX, y_, z_ + moveZ)) { x_ += moveX; z_ += moveZ; return; }
        if (grounded_ && !onLadder()) {
            for (int n=1;n<=16;++n) {
                const float lift=float(n)*.0625f+2*Epsilon;
                if (collides(x_,y_+lift,z_)) break;
                if (collides(x_+moveX,y_+lift,z_+moveZ)) continue;
                y_+=lift; x_+=moveX; z_+=moveZ; grounded_=false;
                moveVertical(-lift);
                return;
            }
        }
        float low = 0, high = 1;
        for (int i = 0; i < 15; ++i) {
            float middle = (low + high) * .5f;
            if (collides(x_ + moveX * middle, y_, z_ + moveZ * middle)) high = middle; else low = middle;
        }
        x_ += moveX * low; z_ += moveZ * low;
    };
    axis(dx, 0); axis(0, dz);
}
bool World::teleport(float x, float y, float z) {
    requireLoaded();
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
    const int w = original_ ? original_->size() : Width;
    const int h = original_ ? OriginalWorld::Height : Height;
    const int d = original_ ? original_->size() : Depth;
    if (x - Radius < 0 || x + Radius >= float(w) || z - Radius < 0 || z + Radius >= float(d) ||
        y < 0 || y + BodyHeight >= float(h)) return false;
    if (original_ && streaming_ && !original_->chunkLoaded(int(std::floor(x)), int(std::floor(z))))
        return false;
    if (collides(x, y, z)) return false;
    x_ = x; y_ = y; z_ = z; velocityY_ = 0;
    grounded_ = collides(x_, y_ - 2 * Epsilon, z_);
    dirty_ = true;
    return true;
}
void World::update(float dt, float forward, float strafe, bool jump, float yawDegrees) {
    requireLoaded();
    if (!std::isfinite(dt) || !std::isfinite(forward) || !std::isfinite(strafe) || !std::isfinite(yawDegrees) || dt < 0)
        fail("invalid movement input");
    dt = std::min(dt, .1f);
    if (dt == 0) return;
    forward = std::clamp(forward, -1.0f, 1.0f); strafe = std::clamp(strafe, -1.0f, 1.0f);
    const float length = std::sqrt(forward * forward + strafe * strafe);
    if (length > 1) { forward /= length; strafe /= length; }
    const float yaw = std::remainder(yawDegrees, 360.0f) * .017453288519943295f;
    const float dx = (std::sin(yaw) * forward + std::cos(yaw) * strafe) * 4.3f;
    const float dz = (std::cos(yaw) * forward - std::sin(yaw) * strafe) * 4.3f;
    const float oldX = x_, oldY = y_, oldZ = z_, oldVelocity = velocityY_;
    grounded_ = collides(x_, y_ - .002f, z_);
    if (jump && !jumpHeld_ && grounded_) { velocityY_ = 7.0f; grounded_ = false; }
    jumpHeld_ = jump;
    const int steps = std::max(1, int(std::ceil(dt / .01f)));
    const float step = dt / float(steps);
    for (int i = 0; i < steps; ++i) {
        const bool ladder=onLadder();
        const float requestX=(ladder?std::clamp(dx,-3.0f,3.0f):dx)*step;
        const float requestZ=(ladder?std::clamp(dz,-3.0f,3.0f):dz)*step;
        const float startX=x_, startZ=z_;
        moveHorizontal(requestX,requestZ);
        const bool horizontalCollision=std::abs(x_-startX-requestX)>.00001f || std::abs(z_-startZ-requestZ)>.00001f;
        grounded_ = false;
        velocityY_ = std::max(-35.0f, velocityY_ - 20.0f * step);
        if (ladder || onLadder()) velocityY_=std::max(velocityY_,-3.0f);
        if (horizontalCollision && onLadder()) velocityY_=4.0f;
        moveVertical(velocityY_ * step);
    }
    if (original_ && streaming_) original_->setStreamingCenter(x_, z_);
    dirty_ = dirty_ || oldX != x_ || oldY != y_ || oldZ != z_ || oldVelocity != velocityY_;
    saveTime_ += dt;
    if (dirty_ && saveTime_ >= 10) save();
}
std::string World::statusJson() const {
    requireLoaded();
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<float>::max_digits10)
        << "{\"loaded\":true,\"path\":" << quote(path_) << ",\"seed\":" << seed_
        << ",\"player\":{\"x\":" << x_ << ",\"y\":" << y_ << ",\"z\":" << z_
        << "},\"size\":[" << width() << ',' << height() << ',' << depth() << "],\"format\":\""
        << (original_ ? OriginalFormat : Format) << "\",\"version\":" << Version
        << ",\"blockCount\":" << blockCount() << ",\"biome\":" << biomeInfo()
        << ",\"structures\":" << (structuresEnabled() ? "true" : "false");
    if (original_) out << ",\"decorations\":" << (decorationsEnabled() ? "true" : "false")
                       << ",\"interactionLimitations\":\"cube-DDA-not-exact-render-shapes;creative-instant-mine-not-survival-drops\""
                       << ",\"blockCountScope\":\"render-window\",\"generation\":"
                       << quote(generatorName(*original_))
                       << ",\"limitations\":" << quote(generatorLimitations(*original_));
    out << '}';
    return out.str();
}
} // namespace mcworld