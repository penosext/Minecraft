/* Host scene tests: extract-scene.py first, then compile with Scene.cpp,
   World.cpp and World/OriginalWorld dependencies plus original_core, using the
   same include paths and -fwrapv -ffp-contract=off -fno-fast-math as the app.
   No SDK, renderer, generated actors or UI required. */
#include "Scene.hpp"
#include "World.hpp"
#include "LegacyCatalog.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;
using Bytes = std::vector<unsigned char>;
using mcscene::Scene;
using mcscene::MobKind;
using mcworld::World;
namespace {
struct Temp {
    std::string path;
    Temp() { char pattern[] = "/tmp/mcscene-test-XXXXXX"; char* value = ::mkdtemp(pattern); assert(value); path = value; }
    ~Temp() { std::error_code error; fs::remove_all(path, error); }
};
template<class F> void rejects(F fn) {
    bool threw = false; try { fn(); } catch (const std::runtime_error&) { threw = true; } assert(threw);
}
Bytes read(const std::string& path) {
    std::ifstream in(path, std::ios::binary); assert(in.good());
    return Bytes(std::istreambuf_iterator<char>(in), {});
}
void write(const std::string& path, const Bytes& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc); assert(out.good());
    out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size())); out.close(); assert(out.good());
}
uint32_t hash(const Bytes& bytes, size_t size) {
    uint32_t value = 2166136261u; for (size_t i = 0; i < size; ++i) value = (value ^ bytes[i]) * 16777619u; return value;
}
void u32(Bytes& bytes, size_t at, uint32_t value) {
    assert(at + 4 <= bytes.size()); for (int i = 0; i < 4; ++i) bytes[at + size_t(i)] = static_cast<unsigned char>(value >> (8 * i));
}
void u64(Bytes& bytes, size_t at, uint64_t value) { u32(bytes, at, uint32_t(value)); u32(bytes, at + 4, uint32_t(value >> 32)); }
void number(Bytes& bytes, size_t at, float value) { uint32_t bits; std::memcpy(&bits, &value, 4); u32(bytes, at, bits); }
void checksum(Bytes& bytes) { u32(bytes, bytes.size() - 4, hash(bytes, bytes.size() - 4)); }
uint16_t blockId(const std::string& key) {
    for (int i = 0; i < legacy::kBlockCount; ++i) if (key == legacy::kBlockSpecs[i].key) return uint16_t(i + 1);
    assert(false); return 0;
}
void field(std::string& json, const std::string& key, uint32_t value) {
    size_t begin = json.find('"' + key + '"'); assert(begin != std::string::npos);
    begin = json.find(':', begin); begin = json.find_first_not_of(" \r\n\t", begin + 1);
    const size_t end = json.find_first_not_of("0123456789", begin);
    json.replace(begin, end - begin, std::to_string(value));
}
struct Fixture {
    Temp root;
    World world;
    std::string path;
    Fixture() {
        world.create(12345, root.path, false);
        path = world.path();
        Bytes chunks = read(path + "/chunks.bin");
        std::fill(chunks.begin() + 40, chunks.end(), 0);
        // Dry grass with a three-block-high wall and a water strip. All starter
        // animals are on this bounded, flat, visible surface near the player.
        for (int z = 0; z < 64; ++z) for (int x = 0; x < 64; ++x) {
            tile(chunks, x, 9, z, blockId(x == 44 ? "calmWater" : "grass"));
            if (x == 40) for (int y = 10; y < 13; ++y) tile(chunks, x, y, z, blockId("stone"));
        }
        Bytes metadata = read(path + "/level.json"); std::string level(metadata.begin(), metadata.end());
        uint32_t ch = hash(chunks, chunks.size()); field(level, "chunksHash", ch);
        write(path + "/chunks.bin", chunks); write(path + "/level.json", Bytes(level.begin(), level.end()));
        Bytes state = read(path + "/state.bin");
        u32(state, 16, ch); number(state, 24, 32.5f); number(state, 28, 10); number(state, 32, 32.5f); number(state, 36, 0); checksum(state);
        write(path + "/state.bin", state);
        world.open(path, root.path);
    }
    static void tile(Bytes& chunks, int x, int y, int z, uint16_t id) {
        size_t at = 40 + ((size_t(y) * 64 + z) * 64 + x) * 2;
        chunks[at] = static_cast<unsigned char>(id); chunks[at + 1] = static_cast<unsigned char>(id >> 8);
    }
    std::string file() const { return path + "/scene.bin"; }
};
void advance(Scene& scene, World& world, int frames, float dt) { for (int i = 0; i < frames; ++i) scene.update(world, dt); }
void clearBodies(const Scene& scene, const World& world) {
    for (const auto& mob : scene.mobs()) {
        const float radius = mob.kind == MobKind::Chicken ? .2f : mob.kind == MobKind::Zombie ? .3f : .45f;
        const float height = mob.kind == MobKind::Chicken ? .7f : mob.kind == MobKind::Zombie ? 1.8f : mob.kind == MobKind::Cow ? 1.4f : mob.kind == MobKind::Sheep ? 1.3f : .9f;
        assert(mob.x >= radius && mob.x <= world.width() - radius && mob.z >= radius && mob.z <= world.depth() - radius);
        assert(mob.y >= 9.999f && mob.y <= 10.001f); // No hovering/flying or climbing 3-high wall.
        for (int y = int(std::floor(mob.y + .0001f)); y <= int(std::floor(mob.y + height - .0001f)); ++y)
            for (int z = int(std::floor(mob.z - radius + .0001f)); z <= int(std::floor(mob.z + radius - .0001f)); ++z)
                for (int x = int(std::floor(mob.x - radius + .0001f)); x <= int(std::floor(mob.x + radius - .0001f)); ++x)
                    assert(world.blockAt(x, y, z) == 0);
        assert(world.blockAt(int(std::floor(mob.x)), 9, int(std::floor(mob.z))) == blockId("grass"));
    }
}
void lifecycle() {
    Fixture f;
    Scene scene;
    World unloaded;
    rejects([&] { scene.open(unloaded); }); rejects([&] { scene.save(f.world); }); rejects([&] { scene.update(f.world, .1f); });
    const Bytes state = read(f.path + "/state.bin"), chunks = read(f.path + "/chunks.bin");
    scene.open(f.world);
    assert(!fs::exists(f.file()) && scene.ticks() == 0 && scene.mobs().size() == 8);
    assert(scene.timeOfDay() > .75f && scene.timeOfDay() < .8f); // original eased dawn
    std::set<MobKind> kinds;
    for (const auto& mob : scene.mobs()) {
        kinds.insert(mob.kind); assert(mob.kind != MobKind::Zombie);
        const float hp = mob.kind == MobKind::Chicken ? 4 : mob.kind == MobKind::Sheep ? 8 : 10;
        assert(mob.health == hp && mob.maxHealth == hp && mob.hurtTime == 0);
    }
    for (auto kind : {MobKind::Pig, MobKind::Cow, MobKind::Sheep, MobKind::Chicken}) assert(kinds.count(kind));
    const auto original = scene.mobs();
    advance(scene, f.world, 100, .05f);
    bool moved = false; for (size_t i = 0; i < original.size(); ++i) if (original[i].x != scene.mobs()[i].x || original[i].z != scene.mobs()[i].z) moved = true;
    assert(moved); clearBodies(scene, f.world);
    scene.update(f.world, .025f); // fractional tick persistence
    scene.save(f.world);
    Scene restored; restored.open(f.world);
    assert(restored.summaryJson() == scene.summaryJson());
    advance(scene, f.world, 50, .05f); advance(restored, f.world, 50, .05f);
    assert(restored.summaryJson() == scene.summaryJson());
    restored.save(f.world);
    assert(read(f.path + "/state.bin") == state && read(f.path + "/chunks.bin") == chunks);
    struct stat info {}; assert(::stat(f.file().c_str(), &info) == 0 && (info.st_mode & 0777) == 0600 && info.st_nlink == 1);
    for (const auto& entry : fs::directory_iterator(f.path)) assert(entry.path().filename().string().find(".scene.bin.tmp-") != 0);
    assert(restored.summaryJson().find("not-original-pathfinding") != std::string::npos);
    rejects([&] { restored.update(f.world, -1); });
    rejects([&] { restored.update(f.world, std::numeric_limits<float>::quiet_NaN()); });
    rejects([&] { restored.update(f.world, std::numeric_limits<float>::infinity()); });
    uint64_t ticks = restored.ticks(); restored.update(f.world, 20); assert(restored.ticks() == ticks + 2); // dt cap
    Scene mismatch; World other; other.create(1, f.root.path, false); rejects([&] { restored.save(other); });
}
void dayAndFrames() {
    Fixture f;
    Scene a, b, c; a.open(f.world); b.open(f.world); c.open(f.world);
    advance(a, f.world, 1200, 1.0f / 60); advance(b, f.world, 400, .05f); advance(c, f.world, 200, .1f);
    assert(a.ticks() == 400 && b.ticks() == 400 && c.ticks() == 400);
    for (size_t i = 0; i < a.mobs().size(); ++i) {
        assert(a.mobs()[i].id == b.mobs()[i].id && a.mobs()[i].x == b.mobs()[i].x && a.mobs()[i].z == b.mobs()[i].z);
        assert(c.mobs()[i].x == b.mobs()[i].x && c.mobs()[i].z == b.mobs()[i].z);
    }
    Scene day; day.open(f.world); const float dawn = day.timeOfDay(), dawnLight = day.daylight();
    advance(day, f.world, 3000, .1f); assert(day.ticks() == 6000 && day.timeOfDay() < .0001f && day.daylight() == 1);
    advance(day, f.world, 6000, .1f); assert(day.ticks() == 18000 && std::abs(day.timeOfDay() - .5f) < .0001f && day.daylight() == .2f);
    assert(std::any_of(day.mobs().begin(), day.mobs().end(), [](const auto& mob) { return mob.kind == MobKind::Zombie; }));
    for (const auto& mob : day.mobs()) if (mob.kind == MobKind::Zombie) assert(std::hypot(mob.x - f.world.playerX(), mob.z - f.world.playerZ()) > 4);
    advance(day, f.world, 3000, .1f); assert(day.ticks() == 24000);
    assert(std::abs(day.timeOfDay() - dawn) < .00001f && std::abs(day.daylight() - dawnLight) < .00001f);
    assert(std::none_of(day.mobs().begin(), day.mobs().end(), [](const auto& mob) { return mob.kind == MobKind::Zombie; }));
    clearBodies(day, f.world);
}
void corruptionAndTargets() {
    Fixture f; Scene good; good.open(f.world); good.save(f.world); const Bytes valid = read(f.file());
    Scene retained; retained.open(f.world); const std::string before = retained.summaryJson();
    auto bad = [&](Bytes bytes) {
        write(f.file(), bytes); rejects([&] { retained.open(f.world); }); assert(retained.summaryJson() == before);
        rejects([&] { good.save(f.world); }); assert(read(f.file()) == bytes); write(f.file(), valid);
    };
    Bytes bytes = valid; bytes[0] ^= 1; bad(bytes);
    bytes = valid; u32(bytes, 8, 4); checksum(bytes); bad(bytes);
    bytes = valid; u32(bytes, 48, 999); checksum(bytes); bad(bytes);
    bytes = valid; number(bytes, 72, std::numeric_limits<float>::quiet_NaN()); checksum(bytes); bad(bytes);
    bytes = valid; number(bytes, 72, -1); checksum(bytes); bad(bytes);
    bytes = valid; u32(bytes, 68, 9); checksum(bytes); bad(bytes);
    bytes = valid; u32(bytes, 124, 1); checksum(bytes); bad(bytes); // duplicate id, v3 records are 60 bytes
    bytes = valid; number(bytes, 92, 100); checksum(bytes); bad(bytes); // upward/invalid velocity
    bytes = valid; u64(bytes, 40, UINT64_MAX); checksum(bytes); bad(bytes); // invalid double remainder
    bytes = valid; bytes.back() ^= 1; bad(bytes);
    bad(Bytes(valid.begin(), valid.end() - 1)); bad({});
    fs::remove(f.file()); fs::create_symlink("state.bin", f.file());
    rejects([&] { good.open(f.world); }); rejects([&] { good.save(f.world); }); fs::remove(f.file());
    fs::create_hard_link(f.path + "/state.bin", f.file());
    rejects([&] { good.open(f.world); }); rejects([&] { good.save(f.world); }); fs::remove(f.file());
    fs::create_directory(f.file()); rejects([&] { good.open(f.world); }); rejects([&] { good.save(f.world); }); fs::remove(f.file());
    assert(::mkfifo(f.file().c_str(), 0600) == 0); rejects([&] { good.open(f.world); }); fs::remove(f.file());
    // A new scene opened while file was absent must not overwrite a later file.
    Scene absent; absent.open(f.world); write(f.file(), valid); rejects([&] { absent.save(f.world); });
    assert(read(f.file()) == valid);
    // Original world directory replaced by another real directory, not only a symlink.
    fs::rename(f.path, f.path + "-old"); fs::create_directory(f.path);
    rejects([&] { retained.save(f.world); }); assert(!fs::exists(f.file()));
    Scene newReader; rejects([&] { newReader.open(f.world); }); // source-World identity, not rediscovered pin
    fs::remove(f.path); fs::rename(f.path + "-old", f.path);
    // Root identity is pinned too, including a replaced (non-symlink) root.
    fs::rename(f.root.path, f.root.path + "-old"); fs::create_directory(f.root.path); fs::create_directory(f.path);
    rejects([&] { retained.save(f.world); }); assert(!fs::exists(f.file()));
    rejects([&] { newReader.open(f.world); }); // source root identity is also pinned
    fs::remove_all(f.root.path); fs::rename(f.root.path + "-old", f.root.path);
}
void gravityAndCollision() {
    Fixture f; Scene scene; scene.open(f.world); scene.save(f.world); Bytes bytes = read(f.file());
    // Controlled high mob above a floor. A valid falling state is accepted;
    // gravity must settle it on the actual dry floor, not hover or tunnel.
    number(bytes, 72, 30.5f); number(bytes, 76, 14); number(bytes, 80, 30.5f);
    number(bytes, 92, -2); u32(bytes, 96, 200); u32(bytes, 100, 0); checksum(bytes);
    write(f.file(), bytes); scene.open(f.world); const int fallingId = scene.mobs()[0].id;
    advance(scene, f.world, 50, .05f);
    auto falling = std::find_if(scene.mobs().begin(), scene.mobs().end(), [&](const auto& m) { return m.id == fallingId; });
    assert(falling != scene.mobs().end() && falling->y >= 9.999f && falling->y < 10.001f);
    // Full day of motion checks body clearance and dry support continually.
    for (int i = 0; i < 2000; ++i) { scene.update(f.world, .05f); clearBodies(scene, f.world); }
    // Structural collision corruption is rejected even with a repaired checksum.
    scene.save(f.world); bytes = read(f.file()); number(bytes, 72, 40.5f); number(bytes, 76, 10); number(bytes, 80, 30.5f); checksum(bytes);
    write(f.file(), bytes); rejects([&] { scene.open(f.world); });
}
} // namespace
int main() {
    lifecycle(); dayAndFrames(); corruptionAndTargets(); gravityAndCollision();
    // One actual original-generation smoke case validates runtime dimensions,
    // absolute coordinates and the nearby-only spawn search on real terrain.
    {
        Temp root; World world; world.create(12345, root.path, true, 864);
        Scene scene; scene.open(world);
        assert(world.width() == 864 && world.height() == 256 && world.depth() == 864);
        assert(!scene.mobs().empty() && scene.mobs().size() <= 8);
        for (const auto& mob : scene.mobs()) assert(std::hypot(mob.x - world.playerX(), mob.z - world.playerZ()) <= 16);
        scene.update(world, .1f); scene.save(world);
        Scene reopened; reopened.open(world); assert(reopened.summaryJson() == scene.summaryJson());
    }
    std::cout << "scene tests passed: original 24000-tick day, elapsed-time invariance, persistence, safe I/O, spawning, movement/collision/gravity\n";
}
