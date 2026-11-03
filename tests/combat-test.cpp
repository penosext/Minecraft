/* Host-only combat tests. Link exactly as scene-test.cpp (Scene, World,
   OriginalWorld and original terrain/biomes/core); no renderer, UI or SDK.
   The controlled scene fixture is an actual backwards-compatible v1 file. */
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
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;
using Bytes = std::vector<unsigned char>;
using mcscene::Mob;
using mcscene::MobKind;
using mcscene::Scene;
using mcworld::World;
namespace {
struct Temp {
    std::string path;
    Temp() { char pattern[] = "/tmp/mccombat-test-XXXXXX"; char* p = ::mkdtemp(pattern); assert(p); path = p; }
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
uint32_t hash(const Bytes& bytes, size_t count) {
    uint32_t h = 2166136261u; for (size_t i = 0; i < count; ++i) h = (h ^ bytes[i]) * 16777619u; return h;
}
void u32(Bytes& bytes, size_t at, uint32_t value) {
    assert(at + 4 <= bytes.size()); for (int i = 0; i < 4; ++i) bytes[at + i] = static_cast<unsigned char>(value >> (8 * i));
}
uint32_t get32(const Bytes& bytes, size_t at) {
    uint32_t n = 0; for (int i = 0; i < 4; ++i) n |= uint32_t(bytes.at(at + i)) << (8 * i); return n;
}
void u64(Bytes& bytes, size_t at, uint64_t value) { u32(bytes, at, uint32_t(value)); u32(bytes, at + 4, uint32_t(value >> 32)); }
void number(Bytes& bytes, size_t at, float value) { uint32_t bits; std::memcpy(&bits, &value, 4); u32(bytes, at, bits); }
void checksum(Bytes& bytes) { u32(bytes, bytes.size() - 4, hash(bytes, bytes.size() - 4)); }
float getFloat(const Bytes& bytes, size_t at) {
    uint32_t bits = get32(bytes, at); float value; std::memcpy(&value, &bits, 4); return value;
}
void field(std::string& json, const std::string& key, uint32_t value) {
    size_t begin = json.find('"' + key + '"'); assert(begin != std::string::npos);
    begin = json.find_first_not_of(" \r\n\t", json.find(':', begin) + 1);
    size_t end = json.find_first_not_of("0123456789", begin); json.replace(begin, end - begin, std::to_string(value));
}
uint16_t blockId(const std::string& key) {
    for (int i = 0; i < legacy::kBlockCount; ++i) if (key == legacy::kBlockSpecs[i].key) return uint16_t(i + 1);
    assert(false); return 0;
}
float maxHealth(MobKind kind) {
    switch (kind) { case MobKind::Pig: case MobKind::Cow: return 10; case MobKind::Sheep: return 8;
    case MobKind::Chicken: return 4; default: return 20; }
}
Mob mob(int id, MobKind kind, float x, float z) { return {id, kind, x, 10, z, 0, 0}; }
struct Fixture {
    Temp root;
    World world;
    std::string path;
    explicit Fixture(bool wall = false, bool water = false) {
        world.create(12345, root.path, false); path = world.path();
        Bytes chunks = read(path + "/chunks.bin"); std::fill(chunks.begin() + 40, chunks.end(), 0);
        for (int z = 0; z < 64; ++z) for (int x = 0; x < 64; ++x) tile(chunks, x, 9, z, blockId("grass"));
        if (wall || water) for (int y = 10; y < 13; ++y) tile(chunks, 32, y, 33, blockId(wall ? "stone" : "calmWater"));
        Bytes metadata = read(path + "/level.json"); std::string level(metadata.begin(), metadata.end());
        uint32_t ch = hash(chunks, chunks.size()); field(level, "chunksHash", ch);
        write(path + "/chunks.bin", chunks); write(path + "/level.json", Bytes(level.begin(), level.end()));
        Bytes state = read(path + "/state.bin"); u32(state, 16, ch);
        number(state, 24, 32.5f); number(state, 28, 10); number(state, 32, 32.5f); number(state, 36, 0); checksum(state);
        write(path + "/state.bin", state); world.open(path, root.path);
    }
    static void tile(Bytes& chunks, int x, int y, int z, uint16_t id) {
        size_t at = 40 + ((size_t(y) * 64 + z) * 64 + x) * 2;
        chunks[at] = static_cast<unsigned char>(id); chunks[at + 1] = static_cast<unsigned char>(id >> 8);
    }
    std::string file() const { return path + "/scene.bin"; }
    void v1(const std::vector<Mob>& mobs) {
        Bytes bytes(52 + mobs.size() * 40 + 4, 0);
        const char magic[8] = {'M','C','S','C','E','N','E',0}; std::memcpy(bytes.data(), magic, 8);
        u32(bytes, 8, 1); u32(bytes, 12, world.seed()); u32(bytes, 16, world.width());
        u32(bytes, 20, world.height()); u32(bytes, 24, world.depth());
        int next = 1; for (const auto& m : mobs) next = std::max(next, m.id + 1);
        u32(bytes, 28, next); u64(bytes, 32, 18000); u32(bytes, 48, uint32_t(mobs.size())); // night keeps Zombie alive
        for (size_t i = 0; i < mobs.size(); ++i) {
            size_t at = 52 + i * 40; const auto& m = mobs[i];
            u32(bytes, at, m.id); u32(bytes, at + 4, uint32_t(m.kind));
            number(bytes, at + 8, m.x); number(bytes, at + 12, m.y); number(bytes, at + 16, m.z);
            number(bytes, at + 20, m.yaw); number(bytes, at + 24, m.walkPhase);
            u32(bytes, at + 32, 200); // initially idle so aim is stable during cooldown tests
        }
        checksum(bytes); write(file(), bytes);
    }
    void v2(const std::vector<Mob>& mobs, uint32_t remaining) {
        v1(mobs); const Bytes old = read(file());
        Bytes bytes(64 + mobs.size() * 56 + 4, 0);
        std::copy_n(old.begin(), 52, bytes.begin()); u32(bytes, 8, 2);
        u64(bytes, 52, 1); u32(bytes, 60, 42); // preserve kills/population cooldown too
        for (size_t i = 0; i < mobs.size(); ++i) {
            const size_t at = 64 + i * 56;
            std::copy_n(old.begin() + 52 + i * 40, 40, bytes.begin() + at);
            number(bytes, at + 40, mobs[i].health); number(bytes, at + 44, maxHealth(mobs[i].kind));
            number(bytes, at + 48, remaining > 10 ? float(remaining - 10) : 0); u32(bytes, at + 52, remaining);
        }
        checksum(bytes); write(file(), bytes);
    }
};
float pitchTo(const World& world, float x, float z, float y = 10.4f) {
    return float(std::atan2(y - (world.playerY() + 1.62), std::hypot(x - world.playerX(), z - world.playerZ())) * 180 / 3.141592653589793);
}
float yawTo(const World& world, float x, float z) {
    return float(std::atan2(x - world.playerX(), z - world.playerZ()) * 180 / 3.141592653589793);
}
void advance(Scene& scene, World& world, int ticks) { for (int i = 0; i < ticks; ++i) scene.update(world, .05f); }
void has(const std::string& json, const std::string& text) { assert(json.find(text) != std::string::npos); }
void targeting() {
    Fixture f; Scene scene;
    const Mob oldAggregate = mob(1, MobKind::Pig, 32.5f, 34.5f);
    assert(oldAggregate.health == 0 && oldAggregate.maxHealth == 0 && oldAggregate.hurtTime == 0);
    // Reversed vector order proves geometric nearest, not first entity wins.
    f.v1({mob(2, MobKind::Zombie, 32.5f, 35.3f), mob(1, MobKind::Pig, 32.5f, 34.5f)}); scene.open(f.world);
    const float pitch = pitchTo(f.world, 32.5f, 34.5f);
    const std::string before = scene.summaryJson();
    has(scene.targetInfo(f.world, 0, pitch), "\"id\":1"); assert(scene.summaryJson() == before);
    has(scene.attack(f.world, 180, 0), "\"hit\":false"); assert(scene.summaryJson() == before);
    assert(scene.targetInfo(f.world, 180, 0) == "{}");
    has(scene.attack(f.world, 0, pitch), "\"health\":9");
    assert(scene.mobs()[0].health == 20 && scene.mobs()[1].id == 1 && scene.mobs()[1].health == 9);
    has(scene.attack(f.world, 0, pitch), "\"damaged\":false");
    // Explicit short reach and hard 3-block maximum, even a caller asks for 100.
    f.v1({mob(1, MobKind::Pig, 32.5f, 34.5f)}); scene.open(f.world);
    has(scene.attack(f.world, 0, pitch, 1), "\"hit\":false");
    has(scene.attack(f.world, 0, pitch, 3), "\"hit\":true");
    f.v1({mob(1, MobKind::Pig, 32.5f, 36.5f)}); scene.open(f.world);
    const float farPitch = pitchTo(f.world, 32.5f, 36.5f);
    has(scene.attack(f.world, 0, farPitch, 100), "\"hit\":false");
    assert(scene.targetInfo(f.world, 0, farPitch) == "{}");
    has(scene.targetInfo(f.world, 0, farPitch, true), "\"id\":1");
    has(scene.attack(f.world, 0, farPitch, 6, true), "\"hit\":true");
    f.v1({mob(1, MobKind::Pig, 32.5f, 39.5f)}); scene.open(f.world);
    has(scene.attack(f.world, 0, pitchTo(f.world, 32.5f, 39.5f), 100, true), "\"hit\":false");
    // Cardinal +X and oblique rays share the World's yaw/pitch convention.
    for (auto point : {std::pair<float,float>{34.5f, 32.5f}, {34.0f, 34.0f}, {30.5f, 32.5f}}) {
        f.v1({mob(1, MobKind::Cow, point.first, point.second)}); scene.open(f.world);
        has(scene.attack(f.world, yawTo(f.world, point.first, point.second), pitchTo(f.world, point.first, point.second)), "\"hit\":true");
    }
    Fixture wall(true); wall.v1({mob(1, MobKind::Pig, 32.5f, 34.5f)}); Scene blocked; blocked.open(wall.world);
    assert(blocked.targetInfo(wall.world, 0, pitch) == "{}"); has(blocked.attack(wall.world, 0, pitch), "\"hit\":false");
    Fixture water(false, true); water.v1({mob(1, MobKind::Pig, 32.5f, 34.5f)}); Scene wet; wet.open(water.world);
    has(wet.attack(water.world, 0, pitch), "\"hit\":true");
}
void speciesAndDeath() {
    for (auto kind : {MobKind::Pig, MobKind::Cow, MobKind::Sheep, MobKind::Chicken, MobKind::Zombie}) {
        Fixture f;
        std::vector<Mob> initial {mob(1, kind, 32.5f, 34.5f)};
        // Occupy the other night-Zombie quota slot outside melee range so the
        // existing 100-tick population pass cannot add mobs during the 22 hits.
        const size_t survivors = kind == MobKind::Zombie ? 1 : 0;
        if (survivors) initial.push_back(mob(2, MobKind::Zombie, 12.5f, 32.5f));
        f.v1(initial); Scene scene; scene.open(f.world);
        assert(scene.mobs()[0].health == maxHealth(kind) && scene.mobs()[0].maxHealth == maxHealth(kind));
        const float pitch = pitchTo(f.world, 32.5f, 34.5f);
        const float damage = kind == MobKind::Zombie ? .92f : 1;
        const int hits = kind == MobKind::Zombie ? 22 : int(maxHealth(kind));
        float expected = maxHealth(kind);
        for (int n = 0; n < hits; ++n) {
            const float yaw = yawTo(f.world, scene.mobs()[0].x, scene.mobs()[0].z);
            const float aim = pitchTo(f.world, scene.mobs()[0].x, scene.mobs()[0].z);
            std::string result = scene.attack(f.world, yaw, aim); has(result, "\"damaged\":true");
            expected = std::max(0.0f, expected - damage);
            if (n + 1 == hits) { has(result, "\"killed\":true"); has(result, "\"health\":0"); assert(scene.mobs().size() == survivors); }
            else {
                has(result, "\"killed\":false"); assert(scene.mobs()[0].id == 1 && scene.mobs()[0].hurtTime == 10);
                assert(std::abs(scene.mobs()[0].health - expected) < .00001f);
                const auto before = scene.summaryJson(); has(scene.attack(f.world, yaw, aim), "\"damaged\":false"); assert(scene.summaryJson() == before);
                advance(scene, f.world, 10); assert(scene.mobs()[0].hurtTime == 0);
            }
        }
        has(scene.summaryJson(), "\"killCount\":1"); assert(scene.targetInfo(f.world, 0, pitch) == "{}");
        has(scene.attack(f.world, 0, pitch), "\"hit\":false");
        scene.save(f.world); Scene restored; restored.open(f.world); assert(restored.summaryJson() == scene.summaryJson());
        advance(restored, f.world, 210); assert(restored.mobs().size() == survivors); // >10s no instant replacement, even after reload
        scene.save(f.world); // save again is deterministic and still no dead record
        Bytes bytes = read(f.file()); assert(get32(bytes, 8) == 3 && get32(bytes, 48) == survivors && get32(bytes, 52) == 1);
        advance(restored, f.world, 190); // next 100-tick population pass after +15s, fresh ids only
        assert(!restored.mobs().empty()); for (const auto& m : restored.mobs()) assert(m.id > 1 && m.health == m.maxHealth);
    }
}
void timersAndPersistence() {
    Fixture f; f.v1({mob(1, MobKind::Chicken, 32.5f, 34.5f)}); Scene scene; scene.open(f.world);
    const float pitch = pitchTo(f.world, 32.5f, 34.5f);
    assert(read(f.file()).size() == 96); // exact 52 + 40 + checksum v1
    has(scene.attack(f.world, 0, pitch), "\"health\":3"); advance(scene, f.world, 5);
    has(scene.attack(f.world, 0, pitch), "\"cooldownTicks\":5"); assert(scene.mobs()[0].health == 3 && scene.mobs()[0].hurtTime == 5);
    scene.save(f.world); assert(read(f.file()).size() == 128); // v3 64 + 60 + checksum
    Scene reload; reload.open(f.world); assert(reload.summaryJson() == scene.summaryJson());
    has(reload.attack(f.world, 0, pitch), "\"damaged\":false");
    advance(reload, f.world, 4); has(reload.attack(f.world, 0, pitch), "\"cooldownTicks\":1"); assert(reload.mobs()[0].health == 3);
    reload.update(f.world, .025f); has(reload.attack(f.world, 0, pitch), "\"damaged\":false");
    reload.update(f.world, .025f); has(reload.attack(f.world, 0, pitch), "\"health\":2"); // exactly 10 ticks, not 20
    advance(reload, f.world, 20); assert(reload.mobs()[0].hurtTime == 0); reload.save(f.world);
    Scene stable; stable.open(f.world); assert(stable.mobs()[0].health == 2 && stable.mobs()[0].id == 1 && stable.mobs()[0].hurtTime == 0);
    // Hurt timers, like AI, must be independent of rendering frame frequency.
    Scene a, b; a.open(f.world); b.open(f.world); a.attack(f.world, 0, pitch); b.attack(f.world, 0, pitch);
    for (int i = 0; i < 30; ++i) a.update(f.world, 1.0f / 60);
    advance(b, f.world, 10); assert(a.summaryJson() == b.summaryJson());
}
void survivorBrainAlignment() {
    Fixture f;
    f.v1({mob(7, MobKind::Chicken, 32.5f, 34.5f), mob(2, MobKind::Cow, 30.5f, 32.5f), mob(9, MobKind::Zombie, 34.5f, 32.5f)});
    Scene scene; scene.open(f.world);
    // A v1-to-v3 migration alone preserves every id/kind and fills all HP.
    scene.save(f.world); Scene migrated; migrated.open(f.world); assert(migrated.summaryJson() == scene.summaryJson());
    for (int hit = 0; hit < 4; ++hit) {
        has(scene.attack(f.world, 0, pitchTo(f.world, 32.5f, 34.5f)), "\"damaged\":true");
        if (hit != 3) advance(scene, f.world, 10);
    }
    assert(scene.mobs().size() == 2 && scene.mobs()[0].id == 2 && scene.mobs()[1].id == 9);
    // Removing the first brain must not leak its cooldown onto the next mob.
    has(scene.attack(f.world, -90, pitchTo(f.world, 30.5f, 32.5f)), "\"damaged\":true");
    has(scene.attack(f.world, 90, pitchTo(f.world, 34.5f, 32.5f)), "\"damaged\":true");
    assert(scene.mobs()[0].health == 9 && std::abs(scene.mobs()[1].health - 19.08f) < .00001f);
    scene.save(f.world); Scene reload; reload.open(f.world); assert(reload.summaryJson() == scene.summaryJson());
    has(reload.attack(f.world, -90, pitchTo(f.world, 30.5f, 32.5f)), "\"damaged\":false");
    advance(scene, f.world, 10); advance(reload, f.world, 10); assert(reload.summaryJson() == scene.summaryJson());
}
void invalidAndUnknownState() {
    Fixture f; f.v1({mob(1, MobKind::Pig, 32.5f, 34.5f)}); Scene scene; scene.open(f.world); scene.save(f.world);
    const Bytes valid = read(f.file()); const std::string before = scene.summaryJson();
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    for (float bad : {nan, inf, -inf}) {
        rejects([&] { scene.attack(f.world, bad, 0); }); rejects([&] { scene.attack(f.world, 0, bad); });
        rejects([&] { scene.attack(f.world, 0, 0, bad); }); rejects([&] { scene.targetInfo(f.world, bad, 0); });
        rejects([&] { scene.targetInfo(f.world, 0, bad); });
    }
    for (float bad : {0.0f, -1.0f}) rejects([&] { scene.attack(f.world, 0, 0, bad); });
    assert(scene.summaryJson() == before);
    scene.attack(f.world, std::numeric_limits<float>::max(), std::numeric_limits<float>::max()); // finite huge angles remain bounded
    Scene closed; rejects([&] { closed.attack(f.world, 0, 0); }); rejects([&] { closed.targetInfo(f.world, 0, 0); });
    auto corrupt = [&](Bytes bytes) {
        checksum(bytes); write(f.file(), bytes); rejects([&] { scene.open(f.world); });
        assert(scene.summaryJson() == before); rejects([&] { scene.save(f.world); }); assert(read(f.file()) == bytes);
        write(f.file(), valid);
    };
    for (float bad : {nan, inf, -1.0f, 0.0f, 11.0f}) { Bytes bytes = valid; number(bytes, 104, bad); corrupt(bytes); }
    for (float bad : {nan, inf, -1.0f, 0.0f, 20.0f}) { Bytes bytes = valid; number(bytes, 108, bad); corrupt(bytes); }
    for (float bad : {nan, inf, -1.0f, 11.0f, .5f, 10.0f}) { Bytes bytes = valid; number(bytes, 112, bad); corrupt(bytes); }
    for (uint32_t bad : {1u, 21u, UINT32_MAX}) { Bytes bytes = valid; u32(bytes, 116, bad); if (bad == 1) number(bytes, 112, 1); corrupt(bytes); }
    Bytes bytes = valid; u64(bytes, 52, UINT64_MAX); corrupt(bytes);
    bytes = valid; u32(bytes, 60, 301); corrupt(bytes);
    bytes = valid; u32(bytes, 12, 999); corrupt(bytes); // seed
    bytes = valid; u32(bytes, 16, 5120); corrupt(bytes); // runtime dimensions
    bytes = valid; u32(bytes, 8, 99); corrupt(bytes); // unknown version
    bytes = valid; bytes.back() ^= 1; write(f.file(), bytes); rejects([&] { scene.open(f.world); }); rejects([&] { scene.save(f.world); });
    write(f.file(), valid);
    // v1 remains parsed/validated, not silently accepted solely for migration.
    f.v1({mob(1, MobKind::Pig, 32.5f, 34.5f)}); bytes = read(f.file()); number(bytes, 60, nan); corrupt(bytes);
}
} // namespace
int main() {
    targeting(); speciesAndDeath(); timersAndPersistence(); survivorBrainAlignment(); invalidAndUnknownState();
    std::cout << "combat tests passed: nearest ray/AABB, miss/wall/liquid/reach, five HP kinds, Zombie armor, cooldown, death/id, v1/v2 persistence, malformed health/NaN/unknown-state safety\n";
}
