// Host regression: compile with Inventory.cpp, InventoryRenderer.cpp and native
// world/renderer dependencies (no UI, SDK, QuickJS or CMake changes required).
// -DMCINVENTORY_CORE_ONLY skips representative-thumbnail pixel assertions.
#include "Inventory.hpp"
#include "ItemCatalog.hpp"
#include "LegacyCatalog.hpp"
#include "World.hpp"
#ifndef MCINVENTORY_CORE_ONLY
#include "InventoryRenderer.hpp"
#endif

#include <algorithm>
#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;
using Bytes = std::vector<unsigned char>;
using mcinventory::Inventory;
using mcinventory::Slot;
namespace {
struct Fixture {
    std::string root;
    mcworld::World world;
    Inventory inv;
    Fixture() {
        char pattern[] = "/tmp/mcitem-state-test-XXXXXX";
        const char* path = ::mkdtemp(pattern); assert(path); root = path;
        world.create(4521, root, false); inv.open(world);
    }
    ~Fixture() { std::error_code error; fs::remove_all(root, error); }
    std::string file() const { return world.path() + "/inventory.bin"; }
};
Bytes read(const std::string& path) {
    std::ifstream file(path, std::ios::binary); assert(file.good());
    return Bytes(std::istreambuf_iterator<char>(file), {});
}
void write(const std::string& path, const Bytes& bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc); assert(file.good());
    file.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    file.close(); assert(file.good());
}
uint32_t hash(const Bytes& bytes, size_t size) {
    uint32_t value = 2166136261u;
    for (size_t i = 0; i < size; ++i) value = (value ^ bytes[i]) * 16777619u;
    return value;
}
uint32_t get32(const Bytes& bytes, size_t at) {
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i) value |= uint32_t(bytes.at(at + i)) << (8 * i);
    return value;
}
void put32(Bytes& bytes, size_t at, uint32_t value) {
    for (int i = 0; i < 4; ++i) bytes.at(at + i) = static_cast<unsigned char>(value >> (8 * i));
}
void append32(Bytes& bytes, uint32_t value) {
    const size_t at = bytes.size(); bytes.resize(at + 4); put32(bytes, at, value);
}
void checksum(Bytes& bytes) { put32(bytes, bytes.size() - 4, hash(bytes, bytes.size() - 4)); }
int state(const std::string& key) {
    for (int i = 0; i < legacy::kBlockCount; ++i) if (key == legacy::kBlockSpecs[i].key) return i;
    assert(false && "missing terrain state fixture"); return -1;
}
const sourceitems::ItemSpec& item(int index) {
    assert(index >= 0 && index < sourceitems::itemCount()); return sourceitems::items()[index];
}
const sourceitems::ItemSpec& stateItem(const std::string& key) { return item(sourceitems::itemForBlockState(state(key))); }
std::string keyFor(const std::string& key) { return stateItem(key).key; }
int storedTotal(const Inventory& inv) {
    int total = 0;
    for (const auto& slot : inv.slots()) total += slot.count;
    for (const auto& slot : inv.armor()) total += slot.count;
    for (const auto& slot : inv.legacyUnresolved()) total += slot.count;
    return total;
}
template<class F> void rejects(F&& fn) {
    bool threw = false; try { fn(); } catch (const std::runtime_error&) { threw = true; } assert(threw);
}
Bytes legacyFile(Fixture& fixture, const std::vector<Slot>& slots, int selected = 0) {
    assert(slots.size() <= 40);
    fixture.inv.save(fixture.world);
    Bytes bytes = read(fixture.file()); bytes.resize(44); put32(bytes, 8, 1); put32(bytes, 32, uint32_t(selected));
    for (int i = 0; i < 40; ++i) {
        const Slot slot = size_t(i) < slots.size() ? slots[i] : Slot {};
        append32(bytes, uint32_t(slot.key.size())); append32(bytes, uint32_t(slot.count));
        bytes.insert(bytes.end(), slot.key.begin(), slot.key.end());
    }
    append32(bytes, hash(bytes, bytes.size())); return bytes;
}
void normalization() {
    Fixture fixture;
    auto same = [&](const std::vector<std::string>& states) {
        assert(!states.empty());
        const auto& expected = stateItem(states.front());
        assert(expected.creative);
#ifndef MCINVENTORY_CORE_ONLY
        std::vector<uint32_t> first;
#endif
        for (const auto& key : states) {
            const auto& spec = stateItem(key);
            assert(std::string(spec.key) == expected.key);
            assert(fixture.inv.selectHotbarBlock(0, key, 1));
            assert(fixture.inv.slots()[0].key == expected.key && fixture.inv.selectedCount() == 1);
            assert(fixture.inv.selectedItemId() == expected.itemId && fixture.inv.selectedAux() == expected.aux);
            assert(fixture.inv.selectedBlockIndex() == expected.renderIndex);
            assert(fixture.inv.selectedPlaceData() == expected.defaultData);
#ifndef MCINVENTORY_CORE_ONLY
            std::vector<uint32_t> pixels(320 * 240, 0xff304050u);
            mcinventory::renderInventoryOverlay(pixels.data(), 320, 240, fixture.inv, false);
            if (first.empty()) first = pixels; else assert(first == pixels);
#endif
        }
    };
    for (const std::string base : {"stairs_wood", "stairs_stone", "woodStairsDark"}) {
        std::vector<std::string> variants {base};
        for (int i = 0; i < 8; ++i) variants.push_back(base + "_data_" + std::to_string(i));
        same(variants);
    }
    for (const std::string species : {"oak", "spruce", "birch", "jungle"})
        same({"log_" + species, "log_" + species + "_x", "log_" + species + "_z"});
    for (const std::string material : {"wood", "iron"}) {
        std::vector<std::string> variants {"door_" + material + "_lower", "door_" + material + "_upper"};
        for (int i = 0; i < 8; ++i) variants.push_back("door_" + material + "_lower_data_" + std::to_string(i));
        for (int i = 8; i < 12; ++i) variants.push_back("door_" + material + "_upper_data_" + std::to_string(i));
        same(variants);
    }
    std::vector<std::string> snow {"topSnow"};
    for (int i = 2; i <= 8; ++i) snow.push_back("snow_layer_" + std::to_string(i));
    same(snow);
    for (int i : {0, 1, 3, 4, 5, 6, 7}) same({"slab_" + std::to_string(i), "slab_" + std::to_string(i) + "_upper", "double_slab_" + std::to_string(i)});
    assert(keyFor("log_oak") != keyFor("log_birch"));
    assert(keyFor("planks_oak") != keyFor("planks_spruce"));
    assert(keyFor("wool_white") != keyFor("wool_blue"));
    assert(keyFor("slab_0") != keyFor("slab_1"));
    assert(keyFor("snow") != keyFor("topSnow"));
    for (int i = 0; i < sourceitems::itemCount(); ++i) {
        const auto& spec = item(i);
        assert(fixture.inv.selectHotbarItem(0, i, spec.maxStack) == spec.creative);
        assert(!fixture.inv.selectHotbarItem(0, i, spec.maxStack + 1));
    }
    const auto before = fixture.inv.infoJson();
    assert(!fixture.inv.selectHotbarItem(0, -1));
    assert(!fixture.inv.selectHotbarItem(0, sourceitems::itemCount()));
    assert(!fixture.inv.selectHotbarBlock(0, "fire", 1));
    assert(!fixture.inv.selectHotbarBlock(0, "water", 1));
    assert(!fixture.inv.selectHotbarBlock(0, "slab_2", 1)); // Registered material, excluded from original creative list.
    assert(fixture.inv.infoJson() == before);
}
void itemStackLimits() {
    Fixture fixture;
    assert(fixture.inv.add(state("stairs_wood_data_7"), 60));
    assert(fixture.inv.add(state("stairs_wood_data_2"), 10));
    assert(fixture.inv.slots()[0].count == 64 && fixture.inv.slots()[1].count == 6);
    assert(fixture.inv.slots()[0].key == fixture.inv.slots()[1].key);
    assert(fixture.inv.consumeSelected(4));
    assert(fixture.inv.transfer(1, 0, std::numeric_limits<int>::max()));
    assert(fixture.inv.slots()[0].count == 64 && fixture.inv.slots()[1].count == 2);
    const auto door = stateItem("door_wood_upper"); assert(door.maxStack == 1);
    assert(!fixture.inv.selectHotbarBlock(2, "door_wood_upper", 2));
    assert(fixture.inv.selectHotbarBlock(2, "door_wood_upper", 1));
    assert(fixture.inv.selectHotbarBlock(3, "door_wood_lower_data_5", 1));
    const auto before = fixture.inv.infoJson();
    assert(!fixture.inv.transfer(2, 3)); assert(fixture.inv.infoJson() == before);
    assert(fixture.inv.transfer(2, 35));
    assert(!fixture.inv.transfer(35, 3));
    Fixture all;
    assert(all.inv.add(state("door_wood_lower"), 36));
    for (const auto& slot : all.inv.slots()) assert(slot.count == 1 && slot.key == door.key);
    const auto full = all.inv.infoJson(); assert(!all.inv.add(state("door_wood_upper"), 1)); assert(all.inv.infoJson() == full);
}
void legacyMigration() {
    Fixture fixture;
    const std::vector<Slot> old = {{"stairs_wood_data_7", 40}, {"stairs_wood_data_2", 40},
        {"door_wood_upper_data_11", 64}, {"log_birch_x", 7}, {"wool_blue", 8},
        {"snow_layer_8", 9}, {"fire", 12}, {"water", 3}, {"planks_spruce", 4}, {"slab_0_upper", 5}};
    const Bytes legacy = legacyFile(fixture, old, 2); write(fixture.file(), legacy);
    const Bytes level = read(fixture.world.path() + "/level.json");
    const Bytes chunks = read(fixture.world.path() + "/chunks.bin");
    const Bytes stateBytes = read(fixture.world.path() + "/state.bin");
    fixture.inv.open(fixture.world);
    assert(read(fixture.file()) == legacy); // Opening is NEVER an implicit save/migration.
    assert(fixture.inv.selectedItemId() == stateItem("door_wood_lower").itemId);
    assert(fixture.inv.selectedBlockIndex() == stateItem("door_wood_lower").renderIndex);
    assert(fixture.inv.selectedCount() == 1 && storedTotal(fixture.inv) == 192);
    assert(fixture.inv.slots()[0].key == fixture.inv.slots()[1].key);
    assert(fixture.inv.slots()[3].key == keyFor("log_birch"));
    assert(fixture.inv.slots()[4].key == keyFor("wool_blue"));
    assert(fixture.inv.slots()[5].key == keyFor("topSnow"));
    assert(fixture.inv.slots()[6].key.empty() && fixture.inv.slots()[7].key.empty());
    assert(fixture.inv.legacyUnresolved().size() == 3);
    assert(fixture.inv.legacyUnresolved()[0].slot == 2 && fixture.inv.legacyUnresolved()[0].key == old[2].key && fixture.inv.legacyUnresolved()[0].count == 63);
    const auto warning = fixture.inv.infoJson();
    assert(warning.find("\"invalidLegacySlots\":[{") != std::string::npos);
    assert(warning.find("\"legacyUnresolved\":[{") != std::string::npos);
    assert(warning.find("\"reason\":\"no-source-item\"") != std::string::npos);
    assert(fixture.inv.select(6)); assert(fixture.inv.emptyHand() && fixture.inv.selectedItemIndex() == -1 && fixture.inv.selectedPlaceData() == -1);
    assert(!fixture.inv.consumeSelected()); assert(!fixture.inv.transfer(6, 7));
    assert(fixture.inv.transfer(0, 1));
    assert(fixture.inv.slots()[0].count == 16 && fixture.inv.slots()[1].count == 64);
    assert(storedTotal(fixture.inv) == 192);
    fixture.inv.save(fixture.world);
    const Bytes v3 = read(fixture.file()); assert(get32(v3, 8) == 3 && v3 != legacy);
    assert(fixture.inv.gameMode() == mcinventory::GameMode::Creative);
    const auto json = fixture.inv.infoJson();
    Inventory restored; restored.open(fixture.world); assert(restored.infoJson() == json);
    assert(storedTotal(restored) == 192); restored.save(fixture.world); assert(read(fixture.file()) == v3);
    assert(read(fixture.world.path() + "/level.json") == level);
    assert(read(fixture.world.path() + "/chunks.bin") == chunks);
    assert(read(fixture.world.path() + "/state.bin") == stateBytes);
    // A stale v1 owner may not overwrite an independently committed migration.
    write(fixture.file(), legacy); Inventory stale, current; stale.open(fixture.world); current.open(fixture.world);
    current.save(fixture.world); const Bytes committed = read(fixture.file());
    rejects([&] { stale.save(fixture.world); }); assert(read(fixture.file()) == committed);
}
void legacyCorruption() {
    Fixture fixture;
    const Bytes good = legacyFile(fixture, {{"stairs_wood_data_5", 64}});
    write(fixture.file(), good); fixture.inv.open(fixture.world); const auto json = fixture.inv.infoJson();
    auto invalid = [&](Bytes bad) {
        write(fixture.file(), bad); rejects([&] { fixture.inv.open(fixture.world); }); assert(fixture.inv.infoJson() == json);
        rejects([&] { fixture.inv.save(fixture.world); }); assert(read(fixture.file()) == bad); write(fixture.file(), good);
    };
    for (uint32_t count : {0u, 65u, 0xffffffffu}) { Bytes bad = good; put32(bad, 48, count); checksum(bad); invalid(bad); }
    { Bytes bad = good; bad[52] = 'X'; checksum(bad); invalid(bad); }
    { Bytes bad = good; put32(bad, 44, 257); checksum(bad); invalid(bad); }
    { Bytes bad = good; put32(bad, 32, 9); checksum(bad); invalid(bad); }
    { Bytes bad = good; put32(bad, 28, 0); checksum(bad); invalid(bad); }
    { Bytes bad = good; bad.pop_back(); invalid(bad); }
    { Bytes bad = good; bad.push_back(0); invalid(bad); }
    // v3 keeps the v2 protected payload layout, after mode/damage fields.
    const Bytes unresolved = legacyFile(fixture, {{"fire", 10}}); write(fixture.file(), unresolved);
    fixture.inv.open(fixture.world); fixture.inv.save(fixture.world);
    const Bytes v3 = read(fixture.file()); const auto protectedJson = fixture.inv.infoJson();
    assert(get32(v3, 528) == 1); // Empty normal slots: protected-count offset.
    auto badProtected = [&](size_t at, uint32_t value) {
        Bytes bad = v3; put32(bad, at, value); checksum(bad); write(fixture.file(), bad);
        rejects([&] { fixture.inv.open(fixture.world); }); assert(fixture.inv.infoJson() == protectedJson);
        rejects([&] { fixture.inv.save(fixture.world); }); assert(read(fixture.file()) == bad); write(fixture.file(), v3);
    };
    badProtected(528, 41); badProtected(532, 40); badProtected(536, 257); badProtected(540, 0); badProtected(540, 65);
    {
        Bytes bad = v3; Bytes duplicate(v3.begin() + 532, v3.end() - 4);
        bad.insert(bad.end() - 4, duplicate.begin(), duplicate.end()); put32(bad, 528, 2); checksum(bad);
        write(fixture.file(), bad); rejects([&] { fixture.inv.open(fixture.world); });
        assert(fixture.inv.infoJson() == protectedJson); rejects([&] { fixture.inv.save(fixture.world); });
        assert(read(fixture.file()) == bad); write(fixture.file(), v3);
    }
}
void protectedCapacityAndArmor() {
    Fixture fixture;
    std::vector<Slot> slots(40, Slot {"door_wood_upper_data_9", 64});
    const Bytes legacy = legacyFile(fixture, slots, 8); write(fixture.file(), legacy);
    fixture.inv.open(fixture.world);
    assert(storedTotal(fixture.inv) == 40 * 64 && fixture.inv.legacyUnresolved().size() == 40);
    for (const auto& slot : fixture.inv.slots()) assert(slot.key == keyFor("door_wood_lower") && slot.count == 1);
    for (const auto& slot : fixture.inv.armor()) assert(slot.key == keyFor("door_wood_lower") && slot.count == 1);
    const auto json = fixture.inv.infoJson(); fixture.inv.save(fixture.world);
    Inventory restored; restored.open(fixture.world); assert(restored.infoJson() == json && storedTotal(restored) == 40 * 64);
    for (int i = 0; i < 40; ++i) assert(restored.legacyUnresolved()[i].slot == i && restored.legacyUnresolved()[i].count == 63);
}
} // namespace
int main() {
    normalization(); itemStackLimits(); legacyMigration(); legacyCorruption(); protectedCapacityAndArmor();
    std::cout << "source item inventory tests passed: state normalization, item maxStack, v1 migration/quarantine, v3 roundtrip and file safety\n";
}
