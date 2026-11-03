/* Standalone host test (no SDK or extra libraries):
   g++ -std=c++17 -O2 -Wall -Wextra -Ijsapi/src/MinecraftRenderer \
       -Ijsapi/build/generated tests/world-test.cpp \
       jsapi/src/MinecraftRenderer/World.cpp -o /tmp/world-test && /tmp/world-test
*/
#include "World.hpp"
#include "BlockCollision.hpp"
#include "LegacyCatalog.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;
using Bytes = std::vector<unsigned char>;
using mcworld::World;
constexpr size_t Cells = size_t(mcworld::Width) * mcworld::Height * mcworld::Depth;

struct Temp {
    std::string path;
    Temp() { char pattern[] = "/tmp/mcworld-test-XXXXXX"; char* p = ::mkdtemp(pattern); assert(p); path = p; }
    ~Temp() { std::error_code error; fs::remove_all(path, error); }
};
template<typename F> void rejects(F function) {
    bool threw = false;
    try { function(); } catch (const std::runtime_error&) { threw = true; }
    assert(threw);
}
Bytes read(const std::string& path) {
    std::ifstream in(path, std::ios::binary); assert(in.good());
    return Bytes(std::istreambuf_iterator<char>(in), {});
}
void write(const std::string& path, const Bytes& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc); assert(out.good());
    out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    out.close(); assert(out.good());
}
std::string text(const std::string& path) { auto bytes = read(path); return std::string(bytes.begin(), bytes.end()); }
void writeText(const std::string& path, const std::string& value) { write(path, Bytes(value.begin(), value.end())); }
uint32_t hash(const Bytes& bytes, size_t size) {
    uint32_t value = 2166136261u;
    for (size_t i = 0; i < size; ++i) value = (value ^ bytes[i]) * 16777619u;
    return value;
}
uint32_t hash(const Bytes& bytes) { return hash(bytes, bytes.size()); }
void u32(Bytes& bytes, size_t offset, uint32_t value) {
    assert(offset + 4 <= bytes.size());
    for (int i = 0; i < 4; ++i) bytes[offset + size_t(i)] = static_cast<unsigned char>(value >> (i * 8));
}
void number(Bytes& bytes, size_t offset, float value) { uint32_t bits; std::memcpy(&bits, &value, 4); u32(bytes, offset, bits); }
uint16_t blockId(const std::string& key) {
    for (int i = 0; i < legacy::kBlockCount; ++i) if (key == legacy::kBlockSpecs[i].key) return uint16_t(i + 1);
    assert(false); return 0;
}
void block(Bytes& chunks, int x, int y, int z, uint16_t id) {
    size_t offset = 40 + ((size_t(y) * mcworld::Depth + z) * mcworld::Width + x) * 2;
    chunks[offset] = static_cast<unsigned char>(id); chunks[offset + 1] = static_cast<unsigned char>(id >> 8);
}
void field(std::string& json, const std::string& key, uint32_t value) {
    size_t begin = json.find('"' + key + '"'); assert(begin != std::string::npos);
    begin = json.find(':', begin); assert(begin != std::string::npos);
    begin = json.find_first_not_of(" \r\n\t", begin + 1); assert(begin != std::string::npos);
    size_t end = json.find_first_not_of("0123456789", begin);
    assert(end != begin); json.replace(begin, end - begin, std::to_string(value));
}
std::vector<std::string> palette(const std::string& level) {
    size_t begin = level.find('[', level.find("\"palette\"")), end = level.find(']', begin);
    std::vector<std::string> keys;
    while ((begin = level.find('"', begin + 1)) < end) {
        size_t closing = level.find('"', begin + 1); assert(closing < end);
        keys.push_back(level.substr(begin + 1, closing - begin - 1)); begin = closing;
    }
    return keys;
}
void palette(std::string& level, const std::vector<std::string>& keys) {
    size_t begin = level.find('[', level.find("\"palette\"")), end = level.find(']', begin);
    std::string list = "[";
    for (const auto& key : keys) { if (list.size() > 1) list += ','; list += '"' + key + '"'; }
    level.replace(begin, end - begin + 1, list + ']');
}
// Repair checksums after deliberate fixture mutations, so tests exercise actual
// structural/id/NaN validation rather than stopping at the first hash check.
void synchronize(const std::string& path, Bytes chunks) {
    std::string level = text(path + "/level.json");
    Bytes joined;
    auto keys = palette(level);
    for (const auto& key : keys) { joined.insert(joined.end(), key.begin(), key.end()); joined.push_back(0); }
    uint32_t ph = hash(joined);
    u32(chunks, 36, ph);
    uint32_t ch = hash(chunks);
    field(level, "paletteHash", ph); field(level, "chunksHash", ch);
    Bytes state = read(path + "/state.bin");
    u32(state, 16, ch); u32(state, 20, ph); u32(state, 40, hash(state, 40));
    write(path + "/chunks.bin", chunks); writeText(path + "/level.json", level); write(path + "/state.bin", state);
}
void player(const std::string& path, float x, float y, float z, float velocity = 0) {
    Bytes state = read(path + "/state.bin");
    number(state, 24, x); number(state, 28, y); number(state, 32, z); number(state, 36, velocity);
    u32(state, 40, hash(state, 40)); write(path + "/state.bin", state);
}
std::vector<uint16_t> snapshot(const World& world) {
    std::vector<uint16_t> result;
    for (int y = 0; y < mcworld::Height; ++y) for (int z = 0; z < mcworld::Depth; ++z) for (int x = 0; x < mcworld::Width; ++x)
        result.push_back(world.blockAt(x, y, z));
    return result;
}
void near(float actual, float expected, float tolerance = .002f) { assert(std::abs(actual - expected) <= tolerance); }
void clearPlayer(const World& world) {
    const float x = world.playerX(), y = world.playerY(), z = world.playerZ();
    assert(x >= .25f && x <= 63.75f && z >= .25f && z <= 63.75f && y >= 0 && y <= 46.2f);
    for (int by = int(std::floor(y + .0001f)); by <= int(std::floor(y + 1.8f - .0001f)); ++by)
        for (int bz = int(std::floor(z - .25f + .0001f)); bz <= int(std::floor(z + .25f - .0001f)); ++bz)
            for (int bx = int(std::floor(x - .25f + .0001f)); bx <= int(std::floor(x + .25f - .0001f)); ++bx) {
                // Source-audited AABBs let nonblocking states (plants, torches,
                // liquids) share the body cells; only real colliders are invalid.
                const uint16_t id = world.blockAt(bx, by, bz);
                const auto boxes = mcworld::blockCollisionBoxes(mcworld::collisionState(id), bx, by, bz);
                for (int i = 0; i < boxes.count; ++i) {
                    assert(!mcworld::collisionIntersects(boxes.boxes[size_t(i)],
                        {x - .25f + .0001f, y + .0001f, z - .25f + .0001f,
                         x + .25f - .0001f, y + 1.8f - .0001f, z + .25f - .0001f}));
                }
            }
}
int main() {
    Temp root, outside;
    World empty;
    assert(!empty.loaded());
    rejects([&] { empty.blockAt(0, 0, 0); }); rejects([&] { empty.path(); }); rejects([&] { empty.seed(); });
    rejects([&] { empty.playerX(); }); rejects([&] { empty.playerY(); }); rejects([&] { empty.playerZ(); });
    rejects([&] { empty.blockCount(); }); rejects([&] { empty.statusJson(); });
    rejects([&] { empty.save(); }); rejects([&] { empty.update(.1f, 0, 0, false, 0); });
    rejects([&] { empty.mine(0, 0); }); rejects([&] { empty.blockData(); }); rejects([&] { empty.revision(); });

    World a, b, other;
    a.create(0x1234abcd, root.path, false); b.create(0x1234abcd, root.path, false); other.create(8, root.path, false);
    assert(a.loaded() && a.path() != b.path());
    assert(a.revision() != 0 && a.revision() != b.revision() && a.revision() != other.revision());
    const auto initialRevision = a.revision();
    const auto* initialData = a.blockData();
    assert(!fs::exists(a.path() + "/edits.bin")); // Existing custom-v1 format remains valid.
    assert(a.path().find(root.path + "/world-") == 0);
    assert(a.seed() == 0x1234abcd);
    struct stat info {}; assert(::stat(a.path().c_str(), &info) == 0 && (info.st_mode & 0777) == 0700);
    assert(read(a.path() + "/chunks.bin").size() == 40 + Cells * 2);
    const auto original = snapshot(a);
    assert(original == snapshot(b) && original != snapshot(other));
    assert(a.blockCount() == int(std::count_if(original.begin(), original.end(), [](uint16_t v) { return v != 0; })));
    for (const std::string key : {"grass", "dirt", "stone", "unbreakable", "sand", "calmWater", "log_oak", "leaves_oak", "coalOre", "ironOre"})
        assert(std::find(original.begin(), original.end(), blockId(key)) != original.end());
    near(a.playerX(), 32.5f); near(a.playerY(), 24); near(a.playerZ(), 32.5f); clearPlayer(a);
    assert(a.blockAt(32, 23, 32) == blockId("grass"));
    assert(a.blockAt(-1, 0, 0) == 0 && a.blockAt(0, -1, 0) == 0 && a.blockAt(64, 0, 0) == 0);
    assert(a.blockAt(0, 48, 0) == 0 && a.blockAt(0, 0, 64) == 0);
    assert(a.blockAt(std::numeric_limits<int>::min(), 0, 0) == 0);
    const std::string status = a.statusJson();
    for (const char* key : {"\"path\":", "\"seed\":", "\"player\":", "\"size\":[64,48,64]", "\"format\":\"mcworld-finite\"", "\"version\":1"})
        assert(status.find(key) != std::string::npos);
    for (int i = 0; i < 10; ++i) a.update(.05f, 1, 0, false, 0);
    a.save(); World loaded; loaded.open(a.path(), root.path);
    assert(snapshot(loaded) == original && loaded.statusJson() == a.statusJson());
    // The legacy custom-v1 finite format has no dimension container and must
    // not acquire one: single dimension, overworld, 44-byte state, no manifest.
    assert(loaded.saveVersion() == World::SaveVersion::V1);
    assert(loaded.dimensions().size() == 1 && loaded.currentDimension() == World::OverworldDimension);
    assert(!fs::exists(a.path() + "/dimensions.bin"));
    assert(read(a.path() + "/state.bin").size() == 44);
    assert(a.revision() == initialRevision && a.blockData() == initialData);
    assert(std::equal(original.begin(), original.end(), a.blockData()));
    assert(loaded.revision() != a.revision());
    World relative; relative.open(fs::path(a.path()).filename().string(), root.path); assert(relative.statusJson() == a.statusJson());

    // Strict filesystem confinement, symlinks (including root ancestors), links,
    // outside roots, and failure must leave a previously loaded object intact.
    const auto preserved = a.statusJson();
    rejects([&] { a.open(other.path(), outside.path); });
    rejects([&] { a.open(root.path, root.path); });
    rejects([&] { a.open(root.path + "/../" + fs::path(outside.path).filename().string(), root.path); });
    rejects([&] { a.open(a.path() + "/../" + fs::path(b.path()).filename().string(), root.path); });
    fs::create_directory_symlink(root.path, outside.path + "/alias");
    rejects([&] { a.open(outside.path + "/alias/" + fs::path(b.path()).filename().string(), outside.path); });
    rejects([&] { empty.create(1, outside.path + "/alias/newroot"); });
    assert(!fs::exists(root.path + "/newroot"));
    fs::create_directory_symlink(b.path(), root.path + "/linked-world");
    rejects([&] { a.open(root.path + "/linked-world", root.path); });
    assert(a.statusJson() == preserved);
    fs::rename(b.path() + "/chunks.bin", b.path() + "/chunks.good");
    fs::create_symlink(b.path() + "/chunks.good", b.path() + "/chunks.bin");
    rejects([&] { a.open(b.path(), root.path); });
    fs::remove(b.path() + "/chunks.bin"); fs::create_hard_link(b.path() + "/chunks.good", b.path() + "/chunks.bin");
    rejects([&] { a.open(b.path(), root.path); });
    fs::remove(b.path() + "/chunks.bin"); fs::rename(b.path() + "/chunks.good", b.path() + "/chunks.bin");

    // Reorder a saved palette AND its stored ids: open must map by key, and a
    // subsequent player save must preserve this original on-disk mapping.
    Bytes reorder = read(b.path() + "/chunks.bin");
    std::string level = text(b.path() + "/level.json");
    auto keys = palette(level); std::swap(keys[0], keys[2]); palette(level, keys); writeText(b.path() + "/level.json", level);
    for (size_t i = 40; i < reorder.size(); i += 2) {
        uint16_t id = uint16_t(reorder[i] | (uint16_t(reorder[i + 1]) << 8));
        if (id == 1) id = 3; else if (id == 3) id = 1;
        reorder[i] = static_cast<unsigned char>(id); reorder[i + 1] = static_cast<unsigned char>(id >> 8);
    }
    synchronize(b.path(), reorder); loaded.open(b.path(), root.path); assert(snapshot(loaded) == original);
    loaded.update(.1f, 1, 0, false, 0); loaded.save(); relative.open(b.path(), root.path); assert(snapshot(relative) == original);
    assert(relative.statusJson() == loaded.statusJson());

    // Corrupt-file tests, including valid checksums around bad ids/header/state.
    const Bytes goodChunks = read(other.path() + "/chunks.bin"), goodState = read(other.path() + "/state.bin");
    const std::string goodLevel = text(other.path() + "/level.json");
    auto reset = [&] { write(other.path() + "/chunks.bin", goodChunks); write(other.path() + "/state.bin", goodState); writeText(other.path() + "/level.json", goodLevel); };
    auto invalid = [&] { rejects([&] { empty.open(other.path(), root.path); }); assert(!empty.loaded()); reset(); };
    for (const size_t size : {size_t(0), size_t(8), size_t(39), goodChunks.size() - 1}) {
        write(other.path() + "/chunks.bin", Bytes(goodChunks.begin(), goodChunks.begin() + std::ptrdiff_t(size))); invalid();
    }
    Bytes bad = goodChunks; bad.push_back(0); write(other.path() + "/chunks.bin", bad); invalid();
    bad = goodChunks; bad[0] ^= 32; synchronize(other.path(), bad); invalid();
    bad = goodChunks; u32(bad, 12, 65); synchronize(other.path(), bad); invalid();
    bad = goodChunks; bad[40] = 255; bad[41] = 255; synchronize(other.path(), bad); invalid();
    bad = goodState; bad[0] = 0; write(other.path() + "/state.bin", bad); invalid();
    for (size_t offset : {size_t(24), size_t(28), size_t(32), size_t(36)}) {
        bad = goodState; number(bad, offset, std::numeric_limits<float>::quiet_NaN()); u32(bad, 40, hash(bad, 40));
        write(other.path() + "/state.bin", bad); invalid();
    }
    player(other.path(), -1, 24, 32.5f); invalid();
    player(other.path(), 32.5f, 0, 32.5f); invalid(); // Inside solid bedrock.
    bad = goodState; bad.push_back(0); write(other.path() + "/state.bin", bad); invalid();
    write(other.path() + "/state.bin", Bytes(goodState.begin(), goodState.end() - 1)); invalid();
    std::string wrong = goodLevel; field(wrong, "version", 999); writeText(other.path() + "/level.json", wrong); invalid();
    wrong = goodLevel; wrong.insert(1, "\"version\":1,"); writeText(other.path() + "/level.json", wrong); invalid();
    wrong = goodLevel; auto unsupported = palette(wrong); unsupported[0] = "unknown_block"; palette(wrong, unsupported);
    writeText(other.path() + "/level.json", wrong); synchronize(other.path(), goodChunks); invalid();
    wrong = goodLevel; unsupported = palette(wrong); unsupported[0] = unsupported[1]; palette(wrong, unsupported);
    writeText(other.path() + "/level.json", wrong); synchronize(other.path(), goodChunks); invalid();
    writeText(other.path() + "/level.json", std::string(1024 * 1024 + 1, ' ')); invalid();

    // Flat fixture lets movement tests distinguish collision/gravity from hills.
    World fixture; fixture.create(77, root.path, false);
    Bytes flat = read(fixture.path() + "/chunks.bin");
    std::fill(flat.begin() + 40, flat.end(), 0);
    for (int z = 0; z < 64; ++z) for (int x = 0; x < 64; ++x) for (int y = 0; y < 10; ++y)
        block(flat, x, y, z, blockId("stone"));
    synchronize(fixture.path(), flat); player(fixture.path(), 32.5f, 10, 32.5f);
    auto loadFixture = [&] { World world; world.open(fixture.path(), root.path); return world; };
    World forward = loadFixture(), right = loadFixture(), diagonal = loadFixture(), yaw90 = loadFixture();
    for (int i = 0; i < 10; ++i) {
        forward.update(.05f, 1, 0, false, 0); right.update(.05f, 0, 1, false, 0);
        diagonal.update(.05f, 1, 1, false, 0); yaw90.update(.05f, 1, 0, false, 90);
    }
    near(forward.playerX(), 32.5f); near(forward.playerZ(), 34.65f); near(forward.playerY(), 10);
    near(right.playerX(), 34.65f); near(right.playerZ(), 32.5f);
    near(yaw90.playerX(), right.playerX()); near(yaw90.playerZ(), right.playerZ());
    near(std::hypot(diagonal.playerX() - 32.5f, diagonal.playerZ() - 32.5f), 2.15f);
    World capped = loadFixture(), normal = loadFixture(); capped.update(100, 1, 0, false, 0); normal.update(.1f, 1, 0, false, 0);
    near(capped.playerZ(), normal.playerZ());
    rejects([&] { normal.update(-1, 0, 0, false, 0); });
    rejects([&] { normal.update(.1f, std::numeric_limits<float>::infinity(), 0, false, 0); });
    rejects([&] { normal.update(.1f, 0, 0, false, std::numeric_limits<float>::quiet_NaN()); });
    World jumper = loadFixture(); jumper.update(.1f, 0, 0, true, 0); assert(jumper.playerY() > 10.3f);
    for (int i = 0; i < 20; ++i) jumper.update(.1f, 0, 0, true, 0);
    near(jumper.playerY(), 10); // Holding jump never re-triggers.
    jumper.update(.02f, 0, 0, false, 0); jumper.update(.1f, 0, 0, true, 0); assert(jumper.playerY() > 10.3f);
    World falling = loadFixture(); player(fixture.path(), 32.5f, 20, 32.5f); falling = loadFixture();
    for (int i = 0; i < 30; ++i) falling.update(.1f, 0, 0, false, 0);
    near(falling.playerY(), 10); clearPlayer(falling);
    player(fixture.path(), 32.5f, 10, 32.5f);

    // A one-cell stair is climbable; a tall wall cannot be tunneled through.
    Bytes obstacles = flat;
    for (int x = 29; x <= 36; ++x) block(obstacles, x, 10, 34, blockId("stone"));
    for (int x = 0; x < 64; ++x) for (int y = 10; y <= 15; ++y) block(obstacles, x, y, 38, blockId("stone"));
    synchronize(fixture.path(), obstacles);
    World walker = loadFixture(); bool stepped = false;
    for (int i = 0; i < 45; ++i) { walker.update(.05f, 1, 0, false, 0); stepped = stepped || walker.playerY() > 10.8f; clearPlayer(walker); }
    assert(stepped); near(walker.playerZ(), 37.75f); near(walker.playerY(), 10);
    float oldX = walker.playerX(); for (int i = 0; i < 5; ++i) walker.update(.05f, 1, 1, false, 0);
    assert(walker.playerX() > oldX + .5f && walker.playerZ() <= 37.751f); clearPlayer(walker);
    synchronize(fixture.path(), flat); player(fixture.path(), 63, 10, 32.5f);
    World boundary = loadFixture(); for (int i = 0; i < 30; ++i) boundary.update(.1f, 0, 1, true, 0);
    near(boundary.playerX(), 63.75f); clearPlayer(boundary);
    player(fixture.path(), 32.5f, 10, 32.5f);
    Bytes roof = flat;
    for (int z = 31; z <= 34; ++z) for (int x = 31; x <= 34; ++x) block(roof, x, 12, z, blockId("stone"));
    synchronize(fixture.path(), roof); World head = loadFixture();
    for (int i = 0; i < 10; ++i) { head.update(.05f, 0, 0, true, 0); assert(head.playerY() <= 10.201f); clearPlayer(head); }

    // Automatic state save, atomic write failures, symlink targets, and replaced
    // directories must not silently report success or destroy the old state.
    synchronize(fixture.path(), flat); player(fixture.path(), 32.5f, 10, 32.5f);
    World autosave = loadFixture();
    for (int i = 0; i < 105; ++i) autosave.update(.1f, .1f, 0, false, 0);
    World saved = loadFixture(); assert(saved.playerZ() > 36.7f);
    const Bytes beforeFailure = read(fixture.path() + "/state.bin");
    struct rlimit previous {}; assert(::getrlimit(RLIMIT_FSIZE, &previous) == 0);
    struct rlimit limited = previous; limited.rlim_cur = 16;
    auto oldSignal = std::signal(SIGXFSZ, SIG_IGN);
    assert(::setrlimit(RLIMIT_FSIZE, &limited) == 0);
    rejects([&] { autosave.save(); });
    assert(::setrlimit(RLIMIT_FSIZE, &previous) == 0); std::signal(SIGXFSZ, oldSignal);
    assert(read(fixture.path() + "/state.bin") == beforeFailure);
    for (const auto& entry : fs::directory_iterator(fixture.path())) assert(entry.path().filename().string().find(".tmp-") == std::string::npos);
    fs::rename(fixture.path() + "/state.bin", fixture.path() + "/state.good");
    fs::create_symlink(fixture.path() + "/state.good", fixture.path() + "/state.bin");
    rejects([&] { autosave.save(); }); assert(read(fixture.path() + "/state.good") == beforeFailure);
    fs::remove(fixture.path() + "/state.bin"); fs::rename(fixture.path() + "/state.good", fixture.path() + "/state.bin");
    fs::rename(fixture.path(), fixture.path() + "-moved"); fs::create_directory(fixture.path());
    rejects([&] { autosave.save(); });
    std::cout << "world-test: deterministic terrain, palette remap, persistence, movement, collision, corruption, confinement and write failures passed\n";
    return 0;
}
