/* Host world-rule tests: run extract-scene.py first, then compile with
   WorldRules.cpp, Scene.cpp, World.cpp and the World/OriginalWorld
   dependencies plus original_core, using the same include paths and
   -fwrapv -ffp-contract=off -fno-fast-math as the app. No SDK, renderer,
   generated actors or UI required.

   Covers: checked defaults, get/set, JSON round trip, unknown-key
   preservation, illegal-document rejection, the four rules that really act
   (DoDaylightCycle, DoMobSpawning, and the DoTileDrops/KeepInventory query
   exports) and old-save compatibility for a world with no rules.json. */
#include "Scene.hpp"
#include "World.hpp"
#include "WorldRules.hpp"
#include "LegacyCatalog.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Bytes = std::vector<unsigned char>;
using mcscene::Scene;
using mcworld::Enforcement;
using mcworld::World;
using mcworld::WorldRule;
using mcworld::WorldRules;

namespace {

constexpr WorldRule kAll[] = {
    WorldRule::DoDaylightCycle, WorldRule::DoTileDrops, WorldRule::DoMobLoot,
    WorldRule::DoMobSpawning, WorldRule::KeepInventory, WorldRule::NaturalRegeneration,
    WorldRule::FireSpreads, WorldRule::MobGriefing, WorldRule::TNT, WorldRule::CheatsEnabled
};

struct Temp {
    std::string path;
    Temp() { char pattern[] = "/tmp/mcrules-test-XXXXXX"; char* value = ::mkdtemp(pattern); assert(value); path = value; }
    ~Temp() { std::error_code error; fs::remove_all(path, error); }
};
template<class F> void rejects(F fn) {
    bool threw = false;
    try { fn(); } catch (const std::runtime_error&) { threw = true; }
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
std::string readText(const std::string& path) {
    const Bytes bytes = read(path);
    return std::string(bytes.begin(), bytes.end());
}
uint32_t hash(const Bytes& bytes, size_t size) {
    uint32_t value = 2166136261u;
    for (size_t i = 0; i < size; ++i) value = (value ^ bytes[i]) * 16777619u;
    return value;
}
void u32(Bytes& bytes, size_t at, uint32_t value) {
    assert(at + 4 <= bytes.size());
    for (int i = 0; i < 4; ++i) bytes[at + size_t(i)] = static_cast<unsigned char>(value >> (8 * i));
}
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
bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

// Same flat dry-grass world the scene tests use, so population and daylight
// behave identically here.
struct Fixture {
    Temp root;
    World world;
    std::string path;
    Fixture() {
        world.create(12345, root.path, false);
        path = world.path();
        Bytes chunks = read(path + "/chunks.bin");
        std::fill(chunks.begin() + 40, chunks.end(), 0);
        for (int z = 0; z < 64; ++z) for (int x = 0; x < 64; ++x) {
            tile(chunks, x, 9, z, blockId("grass"));
        }
        Bytes metadata = read(path + "/level.json");
        std::string level(metadata.begin(), metadata.end());
        const uint32_t ch = hash(chunks, chunks.size());
        field(level, "chunksHash", ch);
        write(path + "/chunks.bin", chunks);
        write(path + "/level.json", Bytes(level.begin(), level.end()));
        Bytes state = read(path + "/state.bin");
        u32(state, 16, ch); number(state, 24, 32.5f); number(state, 28, 10);
        number(state, 32, 32.5f); number(state, 36, 0); checksum(state);
        write(path + "/state.bin", state);
        world.open(path, root.path);
    }
    static void tile(Bytes& chunks, int x, int y, int z, uint16_t id) {
        const size_t at = 40 + ((size_t(y) * 64 + z) * 64 + x) * 2;
        chunks[at] = static_cast<unsigned char>(id);
        chunks[at + 1] = static_cast<unsigned char>(id >> 8);
    }
    std::string rulesFile() const { return path + "/rules.json"; }
    std::string sceneFile() const { return path + "/scene.bin"; }
    std::string levelFile() const { return path + "/level.json"; }
};
void advance(Scene& scene, World& world, int frames, float dt = .05f) {
    for (int i = 0; i < frames; ++i) scene.update(world, dt);
}

// Checked reference host-option defaults: everything on except CheatsEnabled.
// KeepInventory is this port's deliberate product choice (the reference host
// default is off), recorded in WorldRules.hpp.
void defaults() {
    assert(WorldRules::defaultValue(WorldRule::DoDaylightCycle));
    assert(WorldRules::defaultValue(WorldRule::DoTileDrops));
    assert(WorldRules::defaultValue(WorldRule::DoMobLoot));
    assert(WorldRules::defaultValue(WorldRule::DoMobSpawning));
    assert(WorldRules::defaultValue(WorldRule::KeepInventory));
    assert(WorldRules::defaultValue(WorldRule::NaturalRegeneration));
    assert(WorldRules::defaultValue(WorldRule::FireSpreads));
    assert(WorldRules::defaultValue(WorldRule::MobGriefing));
    assert(WorldRules::defaultValue(WorldRule::TNT));
    assert(!WorldRules::defaultValue(WorldRule::CheatsEnabled));

    // A fresh object must equal the declared defaults exactly.
    const WorldRules rules;
    for (WorldRule rule : kAll) assert(rules.get(rule) == WorldRules::defaultValue(rule));

    // Stable, unique, non-empty save keys; name/parse are inverses.
    std::vector<std::string> names;
    for (WorldRule rule : kAll) {
        const std::string text = WorldRules::name(rule);
        assert(!text.empty());
        WorldRule parsed {};
        assert(WorldRules::parse(text, parsed) && parsed == rule);
        names.push_back(text);
    }
    std::sort(names.begin(), names.end());
    assert(std::unique(names.begin(), names.end()) == names.end());
    assert(names.size() == mcworld::WorldRuleCount);
    WorldRule ignored {};
    assert(!WorldRules::parse("doDaylightCycle", ignored)); // case sensitive
    assert(!WorldRules::parse("", ignored));
    assert(!WorldRules::parse("NoSuchRule", ignored));

    // Honest enforcement status: exactly two enforced, two exported, six stored.
    assert(WorldRules::enforcement(WorldRule::DoDaylightCycle) == Enforcement::Enforced);
    assert(WorldRules::enforcement(WorldRule::DoMobSpawning) == Enforcement::Enforced);
    assert(WorldRules::enforcement(WorldRule::DoTileDrops) == Enforcement::ExportedQuery);
    assert(WorldRules::enforcement(WorldRule::KeepInventory) == Enforcement::ExportedQuery);
    for (WorldRule rule : {WorldRule::DoMobLoot, WorldRule::NaturalRegeneration, WorldRule::FireSpreads,
                           WorldRule::MobGriefing, WorldRule::TNT, WorldRule::CheatsEnabled})
        assert(WorldRules::enforcement(rule) == Enforcement::StoredOnly);
}

void readWrite() {
    WorldRules rules;
    for (WorldRule rule : kAll) {
        assert(rules.set(rule, false) && !rules.get(rule));
        assert(rules.set(rule, true) && rules.get(rule));
        assert(rules.set(rule, false) && !rules.get(rule));
    }
    // Setting one rule must not disturb any other.
    WorldRules single;
    assert(single.set(WorldRule::TNT, false));
    for (WorldRule rule : kAll)
        if (rule != WorldRule::TNT) assert(single.get(rule) == WorldRules::defaultValue(rule));

    // Out-of-range enum values are refused, not written out of bounds.
    WorldRules guard;
    const std::string before = guard.toJson();
    assert(!guard.set(WorldRule(mcworld::WorldRuleCount), true));
    assert(!guard.set(WorldRule(999), true));
    assert(!guard.get(WorldRule(999)));
    assert(std::string(WorldRules::name(WorldRule(999))).empty());
    assert(!WorldRules::defaultValue(WorldRule(999)));
    assert(guard.toJson() == before);

    // Integer extras channel, used by Scene for the day-cycle freeze offset.
    WorldRules extras;
    uint64_t value = 7;
    assert(!extras.extra("missing", value) && value == 7);
    assert(extras.setExtra("frozenDayTicks", 4242));
    assert(extras.extra("frozenDayTicks", value) && value == 4242);
    assert(!extras.setExtra("", 1) && !extras.setExtra("bad key", 1) && !extras.setExtra("bad.key", 1));
    assert(extras.clearExtra("frozenDayTicks") && !extras.clearExtra("frozenDayTicks"));
    assert(!extras.extra("frozenDayTicks", value));
}

void jsonRoundTrip() {
    WorldRules rules;
    assert(rules.set(WorldRule::DoDaylightCycle, false));
    assert(rules.set(WorldRule::CheatsEnabled, true));
    assert(rules.set(WorldRule::MobGriefing, false));
    assert(rules.setExtra("frozenDayTicks", 1234567890123ull));
    const std::string text = rules.toJson();

    // Every rule appears by name, with its value.
    for (WorldRule rule : kAll) assert(contains(text, std::string("\"") + WorldRules::name(rule) + "\":"));
    assert(contains(text, "\"DoDaylightCycle\":false"));
    assert(contains(text, "\"CheatsEnabled\":true"));
    assert(contains(text, "\"MobGriefing\":false"));
    assert(contains(text, "\"DoTileDrops\":true"));
    assert(contains(text, "\"frozenDayTicks\":1234567890123"));
    assert(contains(text, "\"version\":1"));

    // The six unenforced rules are reported as such, and nothing else claims
    // to be enforced that is not.
    assert(contains(text, "stored-but-not-enforced"));
    assert(contains(text, "exported-query-caller-enforces"));
    for (WorldRule rule : {WorldRule::DoMobLoot, WorldRule::NaturalRegeneration, WorldRule::FireSpreads,
                           WorldRule::MobGriefing, WorldRule::TNT, WorldRule::CheatsEnabled}) {
        const std::string expected = std::string("\"") + WorldRules::name(rule) + "\":\"stored-but-not-enforced\"";
        assert(contains(text, expected));
    }
    assert(contains(text, "\"DoDaylightCycle\":\"enforced\""));
    assert(contains(text, "\"DoMobSpawning\":\"enforced\""));
    assert(contains(text, "\"DoTileDrops\":\"exported-query-caller-enforces\""));
    assert(contains(text, "\"KeepInventory\":\"exported-query-caller-enforces\""));

    // Values and extras survive, and output is deterministic/idempotent.
    WorldRules loaded;
    assert(loaded.fromJson(text));
    for (WorldRule rule : kAll) assert(loaded.get(rule) == rules.get(rule));
    uint64_t frozen = 0;
    assert(loaded.extra("frozenDayTicks", frozen) && frozen == 1234567890123ull);
    assert(loaded.toJson() == text);
    assert(loaded.fromJson(loaded.toJson()) && loaded.toJson() == text);

    // Partial documents fill from defaults rather than failing.
    WorldRules partial;
    assert(partial.fromJson("{\"version\":1,\"rules\":{\"TNT\":false}}"));
    assert(!partial.get(WorldRule::TNT));
    for (WorldRule rule : kAll)
        if (rule != WorldRule::TNT) assert(partial.get(rule) == WorldRules::defaultValue(rule));
    // Whitespace and an empty document are accepted; empty means all defaults.
    WorldRules empty;
    assert(empty.fromJson(" {\n\t\"rules\" : { \"CheatsEnabled\" : true }\r\n} "));
    assert(empty.get(WorldRule::CheatsEnabled));
    WorldRules bare;
    assert(bare.fromJson("{}") && bare.toJson() == WorldRules().toJson());
    assert(bare.fromJson("{\"rules\":{}}") && bare.toJson() == WorldRules().toJson());
}

void unknownKeysPreserved() {
    // A document from a newer build: unknown rules and unknown top-level keys
    // must come back out unchanged so saving never silently drops them.
    const std::string input =
        "{\"version\":1,\"rules\":{\"TNT\":false,\"FutureRule\":true,\"FutureCount\":42,"
        "\"FutureText\":\"abc\",\"FutureObject\":{\"a\":1},\"FutureList\":[1,2],\"FutureNull\":null},"
        "\"futureTop\":{\"nested\":[true,false]},\"futureFlag\":true}";
    WorldRules rules;
    assert(rules.fromJson(input));
    assert(!rules.get(WorldRule::TNT));
    const std::string text = rules.toJson();
    assert(contains(text, "\"FutureRule\":true"));
    assert(contains(text, "\"FutureCount\":42"));
    assert(contains(text, "\"FutureText\":\"abc\""));
    assert(contains(text, "\"FutureObject\":{\"a\":1}"));
    assert(contains(text, "\"FutureList\":[1,2]"));
    assert(contains(text, "\"FutureNull\":null"));
    assert(contains(text, "\"futureTop\":{\"nested\":[true,false]}"));
    assert(contains(text, "\"futureFlag\":true"));

    // Stable across repeated round trips, so a save/load loop cannot drift.
    WorldRules again;
    assert(again.fromJson(text) && again.toJson() == text);

    // Derived "enforcement" from the input is discarded, never echoed back:
    // a stale or hostile claim cannot make a stored-only rule look enforced.
    WorldRules derived;
    assert(derived.fromJson("{\"version\":1,\"enforcement\":{\"TNT\":\"enforced\"}}"));
    assert(contains(derived.toJson(), "\"TNT\":\"stored-but-not-enforced\""));
    assert(!contains(derived.toJson(), "\"TNT\":\"enforced\""));
}

void illegalRejected() {
    // Every rejection must leave the object exactly as it was.
    WorldRules rules;
    assert(rules.set(WorldRule::CheatsEnabled, true));
    assert(rules.setExtra("frozenDayTicks", 99));
    const std::string before = rules.toJson();
    const char* bad[] = {
        "",                                             // empty input
        "{",                                            // truncated
        "}",                                            // no object
        "[]",                                           // wrong root type
        "null",
        "42",
        "\"text\"",
        "{\"version\":2}",                              // unsupported version
        "{\"version\":0}",
        "{\"version\":\"1\"}",                          // wrong version type
        "{\"version\":-1}",
        "{\"rules\":{\"TNT\":1}}",                      // rule value not boolean
        "{\"rules\":{\"TNT\":\"true\"}}",
        "{\"rules\":{\"TNT\":null}}",
        "{\"rules\":{\"TNT\":true,\"TNT\":false}}",     // duplicate rule key
        "{\"rules\":[]}",                               // rules wrong type
        "{\"rules\":{}}trailing",                       // trailing data
        "{\"rules\":{},}",                              // dangling comma
        "{\"rules\":{\"TNT\":true}",                    // unterminated
        "{\"rules\":{\"\":true}}",                      // empty key
        "{\"rules\":{TNT:true}}",                       // unquoted key
        "{\"rules\":{\"TNT\" true}}",                   // missing colon
        "{\"extras\":{\"a\":-1}}",                      // negative extra
        "{\"extras\":{\"a\":1.5}}",                     // fractional extra
        "{\"extras\":{\"a\":true}}",                    // non-integer extra
        "{\"extras\":{\"a\":01}}",                      // leading zero
        "{\"extras\":{\"bad key\":1}}",                 // illegal extra key
        "{\"extras\":{\"a\":1,\"a\":2}}",               // duplicate extra key
        "{\"extras\":{\"a\":99999999999999999999999}}", // overflow
        "{\"rules\":{},\"rules\":{}}",                  // duplicate field
        "{\"a\":1,\"a\":2}",
        "{\"rules\":{\"TNT\":tru}}",                    // malformed literal
        "{\"rules\":{\"TNT\":true}} {}",
    };
    for (const char* text : bad) {
        assert(!rules.fromJson(text));
        assert(rules.toJson() == before); // untouched after every rejection
    }
    // An oversized document is refused without being parsed.
    assert(!rules.fromJson(std::string(1024 * 1024, 'x')));
    assert(rules.toJson() == before);
    // Too many unknown keys is a rejection, not unbounded growth.
    std::string flood = "{\"rules\":{";
    for (int i = 0; i < 200; ++i) flood += (i ? ",\"Future" : "\"Future") + std::to_string(i) + "\":true";
    flood += "}}";
    assert(!rules.fromJson(flood));
    assert(rules.toJson() == before);
}

// DoDaylightCycle=false must stop celestial time while the simulation keeps
// running, and light must stay consistent with the frozen time.
void daylightCycleEnforced() {
    Fixture f;
    Scene scene;
    scene.open(f.world);
    assert(scene.rule(WorldRule::DoDaylightCycle));

    // Baseline: with the rule enabled, celestial time must actually advance.
    const float startTime = scene.timeOfDay();
    advance(scene, f.world, 200);
    const uint64_t runningTicks = scene.ticks();
    const uint64_t runningDay = scene.dayTicks();
    assert(runningTicks == 200 && runningDay == runningTicks); // in step while enabled
    assert(scene.timeOfDay() != startTime);

    assert(scene.setRule(WorldRule::DoDaylightCycle, false));
    const float frozenTime = scene.timeOfDay(), frozenLight = scene.daylight();
    advance(scene, f.world, 2000); // 100 seconds of simulation
    // The world keeps ticking, the sky does not.
    assert(scene.ticks() == runningTicks + 2000);
    assert(scene.dayTicks() == runningDay);
    assert(scene.timeOfDay() == frozenTime);
    // Lighting is derived from the same frozen clock, so it cannot drift apart.
    assert(scene.daylight() == frozenLight);

    // Re-enabling resumes from the frozen time, never jumping to real elapsed.
    assert(scene.setRule(WorldRule::DoDaylightCycle, true));
    advance(scene, f.world, 400);
    assert(scene.dayTicks() == runningDay + 400);
    assert(scene.timeOfDay() != frozenTime);
    assert(scene.ticks() != scene.dayTicks()); // offset is retained

    // The freeze survives save/open: reloading must not resurrect lost time,
    // and the reloaded sky must match what was saved.
    const uint64_t savedTicks = scene.ticks(), savedDay = scene.dayTicks();
    const float savedTime = scene.timeOfDay(), savedLight = scene.daylight();
    scene.save(f.world);
    assert(fs::exists(f.rulesFile()));
    Scene reloaded;
    reloaded.open(f.world);
    assert(reloaded.ticks() == savedTicks);
    assert(reloaded.dayTicks() == savedDay);
    assert(reloaded.timeOfDay() == savedTime);
    assert(reloaded.daylight() == savedLight);
    assert(reloaded.rule(WorldRule::DoDaylightCycle));

    // A rules.json whose freeze offset exceeds the saved tick count is corrupt.
    std::string text = readText(f.rulesFile());
    const size_t at = text.find("\"frozenDayTicks\":");
    assert(at != std::string::npos);
    const size_t begin = text.find(':', at) + 1;
    const size_t end = text.find_first_not_of("0123456789", begin);
    text.replace(begin, end - begin, std::to_string(savedTicks + 1));
    write(f.rulesFile(), Bytes(text.begin(), text.end()));
    Scene broken;
    rejects([&] { broken.open(f.world); });
}

// DoMobSpawning=false must stop all population, including the initial group.
void mobSpawningEnforced() {
    Fixture f;
    Scene scene;
    scene.open(f.world);
    assert(scene.rule(WorldRule::DoMobSpawning));
    assert(!scene.mobs().empty()); // populated by default
    const size_t populated = scene.mobs().size();

    // Disabling it does not remove existing mobs, but adds no more.
    assert(scene.setRule(WorldRule::DoMobSpawning, false));
    advance(scene, f.world, 1200);
    assert(scene.mobs().size() <= populated);
    const size_t held = scene.mobs().size();
    advance(scene, f.world, 1200);
    assert(scene.mobs().size() <= held); // never grows while disabled
    scene.save(f.world);

    // A world opened with spawning already off must start with no mobs at all.
    Fixture g;
    Scene off;
    off.open(g.world);
    assert(off.setRule(WorldRule::DoMobSpawning, false));
    off.save(g.world);
    // Remove scene.bin so the next open takes the "new scene" path, which
    // calls populate(initial = true); rules.json alone must suppress it.
    fs::remove(g.sceneFile());
    Scene fresh;
    fresh.open(g.world);
    assert(!fresh.rule(WorldRule::DoMobSpawning));
    assert(fresh.mobs().empty()); // initial group suppressed
    advance(fresh, g.world, 2400);
    assert(fresh.mobs().empty());
    assert(fresh.ticks() == 2400); // simulation still ran

    // Re-enabling repopulates, proving the suppression was the rule and not a
    // broken world.
    assert(fresh.setRule(WorldRule::DoMobSpawning, true));
    advance(fresh, g.world, 400);
    assert(!fresh.mobs().empty());
}

// DoTileDrops/KeepInventory are not enforced here; they are exported for the
// gameplay layer. Verify the query surface the caller will wire up.
void queryExports() {
    Fixture f;
    Scene scene;
    scene.open(f.world);
    assert(scene.tileDropsEnabled() && scene.keepInventoryEnabled());
    assert(scene.setRule(WorldRule::DoTileDrops, false));
    assert(!scene.tileDropsEnabled() && scene.keepInventoryEnabled());
    assert(scene.setRule(WorldRule::KeepInventory, false));
    assert(!scene.keepInventoryEnabled());
    assert(scene.setRule(WorldRule::DoTileDrops, true));
    assert(scene.tileDropsEnabled() && !scene.keepInventoryEnabled());
    // Queries agree with the rule store and the reported JSON.
    assert(scene.tileDropsEnabled() == scene.rules().get(WorldRule::DoTileDrops));
    assert(scene.keepInventoryEnabled() == scene.rules().get(WorldRule::KeepInventory));
    assert(contains(scene.rulesJson(), "\"KeepInventory\":false"));
    assert(contains(scene.summaryJson(), "\"rules\":"));

    // JSON application round trip through Scene, and rejection leaves the
    // scene's rules untouched.
    const std::string text = scene.rulesJson();
    Scene other;
    other.open(f.world);
    assert(other.applyRulesJson(text));
    assert(!other.keepInventoryEnabled() && other.tileDropsEnabled());
    const std::string before = other.rulesJson();
    assert(!other.applyRulesJson("{\"rules\":{\"KeepInventory\":1}}"));
    assert(other.rulesJson() == before);
}

// An old save has no rules.json: it must load defaults and stay unwritten.
void oldSaveCompatible() {
    Fixture f;
    const Bytes levelBefore = read(f.levelFile());
    Scene scene;
    scene.open(f.world);
    assert(!fs::exists(f.rulesFile())); // opening creates nothing
    for (WorldRule rule : kAll) assert(scene.rule(rule) == WorldRules::defaultValue(rule));
    assert(scene.dayTicks() == scene.ticks());

    // Saving with untouched default rules must not create the file either, so
    // an old world stays byte-identical for an older build.
    advance(scene, f.world, 100);
    scene.save(f.world);
    assert(fs::exists(f.sceneFile()));
    assert(!fs::exists(f.rulesFile()));

    // level.json is never touched: its field count is exact-match validated,
    // so writing rules there would make this world unopenable.
    assert(read(f.levelFile()) == levelBefore);
    assert(!contains(readText(f.levelFile()), "DoDaylightCycle"));

    // The file appears only once a rule actually differs from the defaults.
    assert(scene.setRule(WorldRule::FireSpreads, false));
    scene.save(f.world);
    assert(fs::exists(f.rulesFile()));
    assert(read(f.levelFile()) == levelBefore);
    Scene reloaded;
    reloaded.open(f.world);
    assert(!reloaded.rule(WorldRule::FireSpreads));
    for (WorldRule rule : kAll)
        if (rule != WorldRule::FireSpreads) assert(reloaded.rule(rule) == WorldRules::defaultValue(rule));

    // A corrupt or hostile rules.json is refused instead of silently resetting
    // the world's rules to defaults.
    write(f.rulesFile(), Bytes({'{'}));
    Scene broken;
    rejects([&] { broken.open(f.world); });
    const std::string unknownVersion = "{\"version\":99}";
    write(f.rulesFile(), Bytes(unknownVersion.begin(), unknownVersion.end()));
    Scene future;
    rejects([&] { future.open(f.world); });

    // An externally modified rules.json is not overwritten blindly.
    const std::string external = "{\"version\":1,\"rules\":{\"TNT\":false}}";
    write(f.rulesFile(), Bytes(external.begin(), external.end()));
    Scene reopened;
    reopened.open(f.world);
    assert(!reopened.rule(WorldRule::TNT));
    const std::string replaced = "{\"version\":1,\"rules\":{\"TNT\":true}}";
    write(f.rulesFile(), Bytes(replaced.begin(), replaced.end()));
    assert(reopened.setRule(WorldRule::MobGriefing, false));
    rejects([&] { reopened.save(f.world); });
}

} // namespace

int main() {
    defaults();
    readWrite();
    jsonRoundTrip();
    unknownKeysPreserved();
    illegalRejected();
    daylightCycleEnforced();
    mobSpawningEnforced();
    queryExports();
    oldSaveCompatible();
    std::cout << "world-rules tests passed\n";
    return 0;
}
