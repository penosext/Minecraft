// Multi-dimension save container tests (format only; no Nether terrain).
//
// Build/link exactly like the other world regressions: compile against
// jsapi/src/MinecraftRenderer plus jsapi/build/generated and link the
// world_runtime set (World.cpp, OriginalWorld.cpp, Inventory.cpp,
// BlockCollision.cpp, ...) with original_decoration, original_structures,
// original_terrain, original_biomes, original_core and pthread. Example:
//
//   cmake --build jsapi/build/native-tests --target world_runtime
//   B=jsapi/build/native-tests
//   g++ -std=c++17 -O1 -UNDEBUG -Ijsapi/src/MinecraftRenderer
//       -Ijsapi/build/generated tests/save-v2-test.cpp $B/libworld_runtime.a
//       $B/terrain/liboriginal_terrain.a $B/structures/liboriginal_structures.a
//       $B/decoration/liboriginal_decoration.a $B/biomes/liboriginal_biomes.a
//       $B/core/liboriginal_core.a -lpthread -o /tmp/save-v2-test
//   /tmp/save-v2-test
//
// What is covered:
//   * a v1 save opens, is played, saves, and remains v1 and byte-identical
//   * v2 round-trips its manifest and the active dimension slot
//   * corrupt/truncated/unknown-field documents are rejected, not guessed at
//   * Inventory still opens and saves across the upgrade
// What is deliberately NOT covered: Nether terrain. This round only proves the
// container can hold more than one dimension.

#include "World.hpp"
#include "Inventory.hpp"
#include "OriginalWorld.hpp"
#include "Scene.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Bytes = std::vector<unsigned char>;
using mcworld::World;

namespace {

struct Temp {
    std::string path;
    Temp() { char pattern[] = "/tmp/mc-save-v2-XXXXXX"; char* p = ::mkdtemp(pattern); assert(p); path = p; }
    ~Temp() { std::error_code error; fs::remove_all(path, error); }
};

template <typename F> void rejects(F function) {
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
std::string text(const std::string& path) { const auto b = read(path); return std::string(b.begin(), b.end()); }
void writeText(const std::string& path, const std::string& value) { write(path, Bytes(value.begin(), value.end())); }
uint32_t hash(const unsigned char* data, size_t count) {
    uint32_t value = 2166136261u;
    for (size_t i = 0; i < count; ++i) value = (value ^ data[i]) * 16777619u;
    return value;
}
// Rewrite the trailing FNV-1a so a mutated state record stays self-consistent.
// Without this every state test would pass for the trivial reason that the
// checksum broke, rather than because the field itself was rejected.
void reseal(Bytes& state) {
    const uint32_t value = hash(state.data(), state.size() - 4);
    for (int i = 0; i < 4; ++i) state[state.size() - 4 + size_t(i)] = static_cast<unsigned char>(value >> (i * 8));
}
uint32_t u32At(const Bytes& bytes, size_t offset) {
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i) value |= uint32_t(bytes[offset + size_t(i)]) << (i * 8);
    return value;
}
// Small, fast original worlds: 864 is the smallest valid size and disabling
// structures/decoration keeps generation cheap. Format behavior is independent
// of the generator baseline, which the generator-matrix case below rechecks.
World fresh(const std::string& root, uint32_t seed = 12345) {
    World world; world.create(seed, root, true, 864, false, false); return world;
}

// --- v1 compatibility -------------------------------------------------------

// A v1 save must open, play and save without changing generation or bytes.
void v1StaysV1() {
    Temp root;
    World created = fresh(root.path);
    const std::string path = created.path();
    assert(created.saveVersion() == World::SaveVersion::V1);
    // v1 on disk: 44-byte state, no "saveVersion"/"dimensions", flat overlays.
    assert(read(path + "/state.bin").size() == 44);
    assert(!fs::exists(path + "/dimensions.bin"));
    assert(!fs::exists(path + "/DIM-1"));
    // A v1 save still reports exactly one dimension and the overworld.
    assert(created.dimensions().size() == 1 && created.dimensions()[0] == World::OverworldDimension);
    assert(created.currentDimension() == World::OverworldDimension);
    assert(created.hasDimension(World::OverworldDimension) && !created.hasDimension(World::NetherDimension));

    const Bytes level = read(path + "/level.json"), state = read(path + "/state.bin");

    // Open, move, mine and save. The save must stay v1, and level.json must be
    // untouched: terrain metadata is immutable, so only state/overlays move.
    World opened; opened.open(path, root.path);
    assert(opened.saveVersion() == World::SaveVersion::V1);
    assert(opened.currentDimension() == World::OverworldDimension);
    for (int i = 0; i < 20; ++i) opened.update(.05f, .4f, 0, false, 45);
    const int x = int(opened.playerX()), z = int(opened.playerZ()), y = int(std::round(opened.playerY())) - 1;
    assert(opened.blockAt(x, y, z) != 0 && opened.mine(0, -90));
    opened.save();
    assert(opened.saveVersion() == World::SaveVersion::V1);
    assert(read(path + "/level.json") == level);
    assert(read(path + "/state.bin").size() == 44);
    assert(!fs::exists(path + "/DIM-1"));

    // Reopen: still v1, still overworld, and the mined cell stayed mined.
    World again; again.open(path, root.path);
    assert(again.saveVersion() == World::SaveVersion::V1 && again.blockAt(x, y, z) == 0);
    assert(again.dimensions().size() == 1 && again.currentDimension() == World::OverworldDimension);

    // A player-only save of an untouched v1 world must not perturb any byte.
    const Bytes levelAfter = read(path + "/level.json"), stateAfter = read(path + "/state.bin");
    World idle; idle.open(path, root.path);
    idle.save();
    assert(read(path + "/level.json") == levelAfter && read(path + "/state.bin") == stateAfter);
    assert(idle.saveVersion() == World::SaveVersion::V1);
    (void)state;
}

// v1 must not gain dimension APIs by accident, and a v1 directory with an
// injected dimension store must be refused rather than quietly ignored.
void v1RejectsDimensionState() {
    Temp root;
    World world = fresh(root.path);
    const std::string path = world.path();
    rejects([&] { world.addDimension(World::NetherDimension); });
    rejects([&] { world.setCurrentDimension(World::NetherDimension); });
    // Registering a dimension on v1 must not have created the store.
    assert(!fs::exists(path + "/DIM-1"));

    // A manifest segment must not appear in a v1 save either.
    {
        World donor = fresh(root.path, 4242);
        donor.upgradeToV2();
        write(path + "/dimensions.bin", read(donor.path() + "/dimensions.bin"));
        rejects([&] { World probe; probe.open(path, root.path); });
        fs::remove(path + "/dimensions.bin");
    }

    const Bytes level = read(path + "/level.json");
    fs::create_directory(path + "/DIM-1");
    rejects([&] { World probe; probe.open(path, root.path); });
    // Saving a loaded v1 world with an injected store is refused too, and the
    // refusal must not have rewritten metadata.
    rejects([&] { world.save(); });
    assert(read(path + "/level.json") == level);
    fs::remove(path + "/DIM-1");
    World recovered; recovered.open(path, root.path);
    assert(recovered.saveVersion() == World::SaveVersion::V1);
}

// The exact-field-count check must still reject unknown fields under v1. This
// is the regression that the v2 branch could most easily have loosened.
void v1RejectsUnknownFields() {
    Temp root;
    World world = fresh(root.path);
    const std::string path = world.path();
    const std::string level = text(path + "/level.json");
    auto restore = [&] { writeText(path + "/level.json", level); };

    // An extra field, even a harmless one, changes the field count.
    std::string extra = level;
    extra.insert(extra.rfind('}'), ",\"nether\":1");
    writeText(path + "/level.json", extra);
    rejects([&] { World probe; probe.open(path, root.path); });
    restore();

    // An inline "dimensions" array is NOT where the manifest lives, so it is
    // just an unknown field and must fail as one.
    std::string orphan = level;
    orphan.insert(orphan.rfind('}'), ",\"dimensions\":[0]");
    writeText(path + "/level.json", orphan);
    rejects([&] { World probe; probe.open(path, root.path); });
    restore();

    // A removed field must fail as well; the count check is an equality.
    const std::string marker = ",\"seaLevel\":63";
    std::string missing = level;
    missing.erase(missing.find(marker), marker.size());
    writeText(path + "/level.json", missing);
    rejects([&] { World probe; probe.open(path, root.path); });
    restore();

    World recovered; recovered.open(path, root.path);
    assert(recovered.saveVersion() == World::SaveVersion::V1);
}

// --- v2 round trip ----------------------------------------------------------

void v2Upgrade() {
    Temp root;
    World world = fresh(root.path);
    const std::string path = world.path();
    const Bytes v1Level = read(path + "/level.json");

    world.upgradeToV2();
    assert(world.saveVersion() == World::SaveVersion::V2);
    // The upgrade ADDS dimensions.bin and grows state.bin to 48 bytes. It must
    // leave level.json byte-identical, because Inventory and other companion
    // stores key their identity to hash(level.json) for original saves.
    assert(read(path + "/level.json") == v1Level);
    assert(read(path + "/dimensions.bin").size() == 28); // 24 + one u32 slot
    assert(read(path + "/state.bin").size() == 48);
    // Upgrade is storage-only: one dimension, overworld active, flat overlays.
    assert(world.dimensions().size() == 1 && world.currentDimension() == World::OverworldDimension);
    assert(!fs::exists(path + "/DIM-1"));
    // Idempotent.
    const Bytes afterUpgrade = read(path + "/dimensions.bin");
    world.upgradeToV2();
    assert(read(path + "/dimensions.bin") == afterUpgrade);
    assert(read(path + "/level.json") == v1Level);

    // The seed, size and generator baseline survive the upgrade unchanged.
    World opened; opened.open(path, root.path);
    assert(opened.saveVersion() == World::SaveVersion::V2);
    assert(opened.seed() == world.seed() && opened.width() == 864 && opened.depth() == 864);
    assert(opened.originalGeneration() && !opened.structuresEnabled() && !opened.decorationsEnabled());
    assert(opened.currentDimension() == World::OverworldDimension);
}

void v2DimensionRoundTrip() {
    Temp root;
    World world = fresh(root.path);
    const std::string path = world.path();
    world.upgradeToV2();

    // Registering a dimension must leave level.json untouched. Inventory keys
    // its identity to hash(level.json) for original saves, so rewriting the
    // document here would lock every companion store out of the world.
    const Bytes levelBeforeAdd = read(path + "/level.json");
    world.addDimension(World::NetherDimension);
    assert(read(path + "/level.json") == levelBeforeAdd);
    assert(world.hasDimension(World::NetherDimension));
    assert(world.dimensions().size() == 2);
    assert(world.dimensions()[0] == 0 && world.dimensions()[1] == 1);
    // The store exists and is named by the upstream DIM-1 convention. It is
    // empty: this round reserves storage and generates no Nether terrain.
    assert(fs::is_directory(path + "/DIM-1"));
    assert(fs::is_empty(path + "/DIM-1"));
    assert(World::dimensionDirectory(World::NetherDimension) == "DIM-1");
    // The overworld deliberately keeps the flat layout, so there is no DIM-0.
    assert(World::dimensionDirectory(World::OverworldDimension).empty());
    assert(!fs::exists(path + "/DIM-0"));
    assert(read(path + "/dimensions.bin").size() == 32); // 24 + two u32 slots
    world.addDimension(World::NetherDimension); // idempotent
    assert(world.dimensions().size() == 2);

    // The manifest survives a reopen.
    World opened; opened.open(path, root.path);
    assert(opened.saveVersion() == World::SaveVersion::V2);
    assert(opened.dimensions().size() == 2 && opened.hasDimension(World::NetherDimension));
    assert(opened.currentDimension() == World::OverworldDimension);

    // The active slot persists. Switching is bookkeeping only: it must not move
    // the player or change the terrain the world reports.
    const float x = opened.playerX(), y = opened.playerY(), z = opened.playerZ();
    opened.setCurrentDimension(World::NetherDimension);
    assert(opened.currentDimension() == World::NetherDimension);
    assert(opened.playerX() == x && opened.playerY() == y && opened.playerZ() == z);
    opened.save();
    assert(read(path + "/state.bin").size() == 48);
    assert(u32At(read(path + "/state.bin"), 40) == uint32_t(World::NetherDimension));

    World reopened; reopened.open(path, root.path);
    assert(reopened.currentDimension() == World::NetherDimension);
    assert(reopened.playerX() == x && reopened.playerZ() == z);
    // And back again.
    reopened.setCurrentDimension(World::OverworldDimension);
    reopened.save();
    World back; back.open(path, root.path);
    assert(back.currentDimension() == World::OverworldDimension);
    assert(u32At(read(path + "/state.bin"), 40) == uint32_t(World::OverworldDimension));

    // Unregistered and out-of-range slots are refused.
    rejects([&] { back.setCurrentDimension(7); });
    rejects([&] { back.addDimension(World::MaxDimensionSlot + 1); });
    rejects([&] { back.addDimension(World::OverworldDimension); });
    rejects([&] { back.addDimension(-1); });
}

// Terrain edits must keep working in a v2 container, and the overworld's flat
// overlays must stay flat so no existing overlay file is ever relocated.
void v2TerrainEditsAndFlatOverlays() {
    Temp root;
    World world = fresh(root.path);
    const std::string path = world.path();
    world.upgradeToV2();
    world.addDimension(World::NetherDimension);

    const int x = int(world.playerX()), z = int(world.playerZ()), y = int(std::round(world.playerY())) - 1;
    assert(world.blockAt(x, y, z) != 0 && world.mine(0, -90));
    world.save();
    // The edit landed in a flat chunk-<key>.bin, not inside DIM-1.
    size_t flat = 0;
    for (const auto& entry : fs::directory_iterator(path))
        flat += entry.path().filename().string().compare(0, 6, "chunk-") == 0;
    assert(flat == 1 && fs::is_empty(path + "/DIM-1"));

    World opened; opened.open(path, root.path);
    assert(opened.blockAt(x, y, z) == 0);
    assert(opened.saveVersion() == World::SaveVersion::V2 && opened.dimensions().size() == 2);
}

// --- v2 rejection cases -----------------------------------------------------

void v2RejectsCorruptMetadata() {
    Temp root;
    World world = fresh(root.path);
    const std::string path = world.path();
    world.upgradeToV2();
    world.addDimension(World::NetherDimension);
    const std::string level = text(path + "/level.json");
    auto restore = [&] { writeText(path + "/level.json", level); };
    auto refuse = [&] { rejects([&] { World probe; probe.open(path, root.path); }); };

    // An unknown extra field under v2 breaks the exact field count, which is
    // the SAME exact count v1 uses because level.json does not differ.
    std::string extra = level;
    extra.insert(extra.rfind('}'), ",\"netherSeed\":7");
    writeText(path + "/level.json", extra); refuse();
    restore();

    // Truncated and empty documents.
    writeText(path + "/level.json", level.substr(0, level.size() / 2)); refuse();
    writeText(path + "/level.json", ""); refuse();
    restore();

    World recovered; recovered.open(path, root.path);
    assert(recovered.saveVersion() == World::SaveVersion::V2 && recovered.dimensions().size() == 2);
}

// The manifest segment carries its own version, binding hash and checksum.
void v2RejectsCorruptManifest() {
    Temp root;
    World world = fresh(root.path);
    const std::string path = world.path();
    world.upgradeToV2();
    world.addDimension(World::NetherDimension);
    const Bytes manifest = read(path + "/dimensions.bin");
    assert(manifest.size() == 32);
    auto restore = [&] { write(path + "/dimensions.bin", manifest); };
    auto refuse = [&] { rejects([&] { World probe; probe.open(path, root.path); }); };

    // Slot list must be ascending, unique, in range and start at the overworld.
    // Each variant is resealed so only the content is at fault.
    auto withSlots = [&](const std::vector<uint32_t>& slots) {
        Bytes bytes(manifest.begin(), manifest.begin() + 20);
        for (int i = 0; i < 4; ++i) bytes[20 + size_t(i)] = static_cast<unsigned char>(slots.size() >> (i * 8));
        for (uint32_t slot : slots)
            for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<unsigned char>(slot >> (i * 8)));
        bytes.resize(bytes.size() + 4);
        reseal(bytes);
        write(path + "/dimensions.bin", bytes);
    };
    withSlots({1, 0}); refuse();          // unordered
    withSlots({0, 0}); refuse();          // duplicate
    withSlots({0, 1, 1}); refuse();       // duplicate tail
    withSlots({0, 2, 1}); refuse();       // unordered tail
    withSlots({1}); refuse();             // omits the overworld
    withSlots({0, 16}); refuse();         // out of range
    withSlots({0, 99}); refuse();         // far out of range
    withSlots({}); refuse();              // empty
    restore();

    // Count that disagrees with the file length.
    Bytes miscount = manifest;
    miscount[20] = 3; reseal(miscount);
    write(path + "/dimensions.bin", miscount); refuse();
    restore();

    // Truncation, bad magic, wrong version, and a flipped slot left unsealed.
    for (size_t size : {size_t(0), size_t(8), size_t(23), size_t(28), size_t(31)}) {
        write(path + "/dimensions.bin", Bytes(manifest.begin(), manifest.begin() + long(size))); refuse();
    }
    Bytes magic = manifest; magic[0] = 0; reseal(magic);
    write(path + "/dimensions.bin", magic); refuse();
    Bytes version = manifest; version[8] = 9; reseal(version);
    write(path + "/dimensions.bin", version); refuse();
    Bytes unsealed = manifest; unsealed[24] = static_cast<unsigned char>(unsealed[24] ^ 1u);
    write(path + "/dimensions.bin", unsealed); refuse();
    restore();

    // The manifest is bound to this save's level.json hash, so one lifted from
    // a different world is rejected instead of being adopted.
    World other = fresh(root.path, 4242);
    other.upgradeToV2();
    other.addDimension(World::NetherDimension);
    write(path + "/dimensions.bin", read(other.path() + "/dimensions.bin")); refuse();
    restore();

    // A missing manifest in a v2 save is corruption, not an empty manifest.
    fs::remove(path + "/dimensions.bin"); refuse();
    restore();

    World recovered; recovered.open(path, root.path);
    assert(recovered.dimensions().size() == 2 && recovered.hasDimension(World::NetherDimension));
}

void v2RejectsCorruptState() {
    Temp root;
    World world = fresh(root.path);
    const std::string path = world.path();
    world.upgradeToV2();
    world.addDimension(World::NetherDimension);
    const Bytes state = read(path + "/state.bin");
    assert(state.size() == 48);
    auto restore = [&] { write(path + "/state.bin", state); };
    auto refuse = [&] { rejects([&] { World probe; probe.open(path, root.path); }); };

    // A v2 metadata document with a 44-byte v1 state record is a mismatch, not
    // something to interpret as the overworld.
    write(path + "/state.bin", Bytes(state.begin(), state.begin() + 44)); refuse();
    // Truncated and over-long records, both resealed so only length is at fault.
    for (size_t size : {size_t(40), size_t(44), size_t(47)}) {
        Bytes shorter(state.begin(), state.begin() + long(size));
        reseal(shorter); write(path + "/state.bin", shorter); refuse();
    }
    Bytes longer = state; longer.push_back(0); reseal(longer);
    write(path + "/state.bin", longer); refuse();
    restore();

    // An undeclared or out-of-range slot in a correctly checksummed record.
    for (uint32_t slot : {uint32_t(2), uint32_t(7), uint32_t(16), uint32_t(0xffffffffu)}) {
        Bytes bad = state;
        for (int i = 0; i < 4; ++i) bad[40 + size_t(i)] = static_cast<unsigned char>(slot >> (i * 8));
        reseal(bad); write(path + "/state.bin", bad); refuse();
    }
    restore();

    // The slot is inside the checksummed region, so flipping it without
    // resealing fails the checksum too.
    Bytes unsealed = state;
    unsealed[40] = static_cast<unsigned char>(unsealed[40] ^ 1u);
    write(path + "/state.bin", unsealed); refuse();
    restore();

    // Bad magic still fails under v2.
    Bytes magic = state; magic[0] = 0; reseal(magic);
    write(path + "/state.bin", magic); refuse();
    restore();

    World recovered; recovered.open(path, root.path);
    assert(recovered.currentDimension() == World::OverworldDimension);
}

// An undeclared dimension store must be rejected on both open and save. This is
// the check that keeps requirement 4's relaxed enumeration from becoming a hole.
void v2RejectsUndeclaredStores() {
    Temp root;
    World world = fresh(root.path);
    const std::string path = world.path();
    world.upgradeToV2();

    // DIM-1 on disk but not in the manifest.
    fs::create_directory(path + "/DIM-1");
    rejects([&] { World probe; probe.open(path, root.path); });
    rejects([&] { world.save(); });
    fs::remove(path + "/DIM-1");
    World fine; fine.open(path, root.path);
    assert(fine.saveVersion() == World::SaveVersion::V2);

    // Declaring DIM-1 then removing the store leaves the manifest valid; the
    // store holds no terrain yet, so a missing empty store is not corruption.
    fine.addDimension(World::NetherDimension);
    fs::remove(path + "/DIM-1");
    World without; without.open(path, root.path);
    assert(without.hasDimension(World::NetherDimension));

    // A DIM-* symlink is an injection attempt, not a dimension store.
    fs::create_symlink(root.path, path + "/DIM-2");
    rejects([&] { World probe; probe.open(path, root.path); });
    fs::remove(path + "/DIM-2");
    // DIM-0 must never alias the overworld's flat layout.
    fs::create_directory(path + "/DIM-0");
    World ignoresDimZero; ignoresDimZero.open(path, root.path);
    assert(ignoresDimZero.dimensions().size() == 2);
    fs::remove(path + "/DIM-0");
}

// Every generator baseline must upgrade, since the v1 field count differs
// between decoration (15) and non-decoration (14) saves.
void v2AcrossGenerators() {
    Temp root;
    for (int variant = 0; variant < 3; ++variant) {
        const bool structures = variant >= 1, decoration = variant == 2;
        World world;
        world.create(999 + uint32_t(variant), root.path, true, 864, structures, decoration);
        const std::string path = world.path();
        assert(world.saveVersion() == World::SaveVersion::V1);
        world.upgradeToV2();
        world.addDimension(World::NetherDimension);
        World opened; opened.open(path, root.path);
        assert(opened.saveVersion() == World::SaveVersion::V2);
        assert(opened.dimensions().size() == 2);
        assert(opened.structuresEnabled() == structures && opened.decorationsEnabled() == decoration);
        // Decoration saves carry an extra "structures" field, so the exact
        // field count differs by baseline; both must still open as v2.
        assert(fs::exists(path + "/dimensions.bin"));
        assert(decoration == (text(path + "/level.json").find("\"structures\":") != std::string::npos));
    }
}

// Legacy custom-v1 finite worlds have no dimension container and must say so
// rather than half-supporting one.
void customSavesUnsupported() {
    Temp root;
    World legacy; legacy.create(7, root.path, false); legacy.save();
    assert(legacy.saveVersion() == World::SaveVersion::V1);
    rejects([&] { legacy.upgradeToV2(); });
    rejects([&] { legacy.addDimension(World::NetherDimension); });
    rejects([&] { legacy.setCurrentDimension(World::NetherDimension); });
    const std::string path = legacy.path();
    legacy.save();
    World opened; opened.open(path, root.path);
    assert(opened.saveVersion() == World::SaveVersion::V1);
    assert(read(path + "/state.bin").size() == 44);
    assert(opened.dimensions().size() == 1);
}

// --- Inventory across the upgrade -------------------------------------------

// Inventory keys itself to world identity, including hash(level.json) as its
// source hash for original saves, so an upgrade that rewrites metadata must be
// checked against it explicitly.
void inventorySurvivesUpgrade() {
    Temp root;
    World world = fresh(root.path);
    const std::string path = world.path();

    // v1: inventory opens, puts a known stack in a hotbar slot, saves, reloads.
    {
        mcinventory::Inventory inventory;
        inventory.open(world);
        assert(inventory.selectHotbarBlock(0, "stone", 10));
        inventory.save(world);
        mcinventory::Inventory reopened; reopened.open(world);
        assert(reopened.select(0) && reopened.selectedCount() == 10);
    }

    world.upgradeToV2();
    assert(world.saveVersion() == World::SaveVersion::V2);

    // After the upgrade an inventory must still open against the same world.
    // Its stored source hash is hash(level.json), which the upgrade rewrote, so
    // this is exactly where a cascade failure would surface.
    mcinventory::Inventory afterUpgrade;
    afterUpgrade.open(world);
    assert(afterUpgrade.select(0) && afterUpgrade.selectedCount() == 10);
    assert(afterUpgrade.selectHotbarBlock(1, "dirt", 3));
    afterUpgrade.save(world);

    mcinventory::Inventory reloaded;
    reloaded.open(world);
    assert(reloaded.select(1) && reloaded.selectedCount() == 3);

    // And across a reopen of the upgraded world, plus a dimension switch, which
    // rewrites state.bin to 48 bytes under the inventory's feet.
    World opened; opened.open(path, root.path);
    opened.addDimension(World::NetherDimension);
    opened.setCurrentDimension(World::NetherDimension);
    opened.save();
    mcinventory::Inventory inNether;
    inNether.open(opened);
    assert(inNether.select(1) && inNether.selectedCount() == 3);
    assert(inNether.selectHotbarBlock(2, "sand", 5));
    inNether.save(opened);

    World back; back.open(path, root.path);
    assert(back.currentDimension() == World::NetherDimension);
    mcinventory::Inventory finalInventory;
    finalInventory.open(back);
    assert(finalInventory.select(1) && finalInventory.selectedCount() == 3);
    assert(finalInventory.select(2) && finalInventory.selectedCount() == 5);
}

// Scene persists only scene.bin/rules.json and never reads state.bin or
// level.json, so it should need no changes. Asserted rather than assumed.
void sceneSurvivesUpgrade() {
    Temp root;
    World world = fresh(root.path);
    const std::string path = world.path();
    { mcscene::Scene scene; scene.open(world); scene.save(world); }

    world.upgradeToV2();
    { mcscene::Scene scene; scene.open(world); scene.save(world); }

    World opened; opened.open(path, root.path);
    opened.addDimension(World::NetherDimension);
    opened.setCurrentDimension(World::NetherDimension);
    opened.save();
    { mcscene::Scene scene; scene.open(opened); scene.save(opened); }

    // Scene and Inventory coexist in the same upgraded world directory.
    mcinventory::Inventory inventory;
    inventory.open(opened);
    assert(inventory.selectHotbarBlock(0, "stone", 2));
    inventory.save(opened);
    mcscene::Scene scene; scene.open(opened); scene.save(opened);
    mcinventory::Inventory reloaded; reloaded.open(opened);
    assert(reloaded.select(0) && reloaded.selectedCount() == 2);
}

} // namespace

int main() {
    v1StaysV1();
    v1RejectsDimensionState();
    v1RejectsUnknownFields();
    v2Upgrade();
    v2DimensionRoundTrip();
    v2TerrainEditsAndFlatOverlays();
    v2RejectsCorruptMetadata();
    v2RejectsCorruptManifest();
    v2RejectsCorruptState();
    v2RejectsUndeclaredStores();
    v2AcrossGenerators();
    customSavesUnsupported();
    inventorySurvivesUpgrade();
    sceneSurvivesUpgrade();
    std::cout << "save-v2-test: v1 byte-identical compatibility, v2 upgrade/manifest/dimension "
                 "round trip, flat overworld overlays, corrupt/truncated/unknown rejection and "
                 "inventory continuity passed\n";
    return 0;
}
