// Device-only smoke probe; no SDK, UI, command-line paths or existing saves.
// Link against native-tests renderer_runtime, world_runtime and original modules.
// Keep console/file I/O in stdio: GCC 13 <iostream> initialization needs a newer
// libstdc++ than the device. Checks remain active even when built with -DNDEBUG.
#include "Inventory.hpp"
#include "InventoryRenderer.hpp"
#include "ItemCatalog.hpp"
#include "ItemPlacement.hpp"
#include "LegacyCatalog.hpp"
#include "World.hpp"

#include <array>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
const char* stage = "startup";
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void jsonString(const char* text) {
    std::putchar('"');
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p) {
        if (*p == '"' || *p == '\\') std::printf("\\%c", *p);
        else if (*p < 32) std::printf("\\u%04x", unsigned(*p));
        else std::putchar(*p);
    }
    std::putchar('"');
}
struct TemporaryRoot {
    char path[64] = "/tmp/minecraft-items-probe.XXXXXX";
    bool active = false;
    void create() {
        if (!::mkdtemp(path)) throw std::runtime_error(std::string("mkdtemp: ") + std::strerror(errno));
        active = true;
    }
    bool cleanup() {
        if (!active) return true;
        std::error_code error;
        std::filesystem::remove_all(path, error); // Only our mkdtemp tree, never a caller path.
        if (error) {
            std::printf("{\"event\":\"cleanup\",\"ok\":false,\"errno\":%d}\n", error.value());
            return false;
        }
        active = false;
        return true;
    }
    ~TemporaryRoot() { if (active) cleanup(); }
};
int itemIndex(int id, int aux = 0) {
    const int index = sourceitems::itemIndexForIdAux(id, aux);
    require(index >= 0, "missing source item");
    return index;
}
const sourceitems::ItemSpec& item(int id, int aux = 0) {
    return sourceitems::items()[itemIndex(id, aux)];
}
int stateIndex(int tile, int data) {
    const int index = mcworld::placementState(tile, data);
    require(index >= 0 && index < legacy::kBlockCount, "missing placement state");
    require(legacy::kBlockSpecs[index].tileId == tile && legacy::kBlockSpecs[index].data == data,
            "placement state metadata mismatch");
    return index;
}
bool stairs(int tile) {
    for (int id : {53, 67, 109, 114, 128, 134}) if (tile == id) return true;
    return false;
}
void canonical(const mcinventory::Inventory& inv) {
    for (const auto& slot : inv.slots()) {
        if (!slot.count) { require(slot.key.empty(), "noncanonical empty slot"); continue; }
        const auto* spec = sourceitems::findItem(slot.key);
        require(spec && slot.key == spec->key && slot.count > 0 && slot.count <= spec->maxStack,
                "inventory leaked world metadata or invalid count");
    }
    for (const auto& slot : inv.armor()) require(slot.count == 0 && slot.key.empty(), "unexpected armor");
    require(inv.legacyUnresolved().empty(), "unexpected legacy quarantine in fresh world");
}
void selected(const mcinventory::Inventory& inv, int id, int aux, int count) {
    const auto& spec = item(id, aux);
    const bool matches = inv.selectedItemIndex() == itemIndex(id, aux) && inv.selectedItemId() == id &&
                         inv.selectedAux() == aux && inv.selectedCount() == count &&
                         inv.selectedBlockIndex() == spec.renderIndex && inv.selectedPlaceData() == spec.defaultData &&
                         inv.slots()[inv.selectedSlot()].key == spec.key;
    if (!matches)
        std::printf("{\"event\":\"selection-mismatch\",\"expected\":[%d,%d,%d,%d,%d,%d],\"actual\":[%d,%d,%d,%d,%d,%d]}\n",
                    itemIndex(id, aux), id, aux, count, spec.renderIndex, spec.defaultData,
                    inv.selectedItemIndex(), inv.selectedItemId(), inv.selectedAux(), inv.selectedCount(),
                    inv.selectedBlockIndex(), inv.selectedPlaceData());
    require(matches, "selected item is not canonical");
    canonical(inv);
}
void catalog() {
    stage = "catalog";
    require(sourceitems::itemCount() == 126, "expected 126 source items");
    require(sourceitems::blockStateCount() == 312 && legacy::kBlockCount == 312, "expected 312 states");
    int creative = 0, stairStates = 0;
    for (int i = 0; i < sourceitems::itemCount(); ++i) creative += sourceitems::items()[i].creative;
    require(creative == 125, "expected 125 creative items");
    require(item(324).maxStack == 1 && item(324).creative, "wood door 324 must stack to one");
    std::array<unsigned, 6> seen {};
    const int ids[] = {53, 67, 109, 114, 128, 134};
    for (int i = 0; i < sourceitems::blockStateCount(); ++i) {
        const auto& s = sourceitems::blockStates()[i];
        const auto& block = legacy::kBlockSpecs[i];
        require(s.renderIndex == i && std::strcmp(s.renderCatalogKey, block.key) == 0 &&
                s.tileId == block.tileId && s.data == block.data, "state/catalog mismatch");
        if (!stairs(s.tileId)) continue;
        require(s.data >= 0 && s.data < 8 && s.itemIndex == itemIndex(s.tileId) &&
                sourceitems::itemForBlockState(i) == itemIndex(s.tileId), "stairs state split into separate items");
        for (int j = 0; j < 6; ++j) if (ids[j] == s.tileId) seen[j] |= 1u << s.data;
        ++stairStates;
    }
    for (unsigned mask : seen) require(mask == 255, "missing stairs direction/upper states");
    require(stairStates == 54, "expected six stairs families, each representative plus eight states");
    for (int id : {35, 5, 17}) {
        const int variants = id == 35 ? 16 : 4;
        for (int aux = 0; aux < variants; ++aux) {
            const auto& spec = item(id, aux);
            require(spec.creative && sourceitems::itemForBlockState(stateIndex(id, aux)) == itemIndex(id, aux),
                    "material subtype lost");
            for (int previous = 0; previous < aux; ++previous)
                require(std::strcmp(spec.key, item(id, previous).key) != 0, "material identities collapsed");
            if (id == 17) for (int axis : {4, 8})
                require(sourceitems::itemForBlockState(stateIndex(id, aux | axis)) == itemIndex(id, aux),
                        "log axis leaked into item aux");
        }
    }
    std::printf("{\"event\":\"catalog\",\"creative\":%d,\"source\":126,\"states\":312,\"stairsStates\":%d,\"wool\":16,\"planks\":4,\"logs\":4,\"door324Max\":1}\n",
                creative, stairStates);
}
void creativeSelection(mcinventory::Inventory& inv) {
    stage = "creative-selection";
    require(inv.opened() && inv.emptyHand(), "new inventory must be empty");
    for (const auto& slot : inv.slots()) require(slot.count == 0 && slot.key.empty(), "unexpected starter kit");
    require(inv.select(0), "select hotbar zero");
    for (int i = 0; i < sourceitems::itemCount(); ++i) {
        const auto& spec = sourceitems::items()[i];
        const auto before = inv.slots();
        require(inv.selectHotbarItem(0, i, spec.maxStack) == spec.creative, "creative availability mismatch");
        if (spec.creative) selected(inv, spec.itemId, spec.aux, spec.maxStack);
        else require(inv.slots()[0].key == before[0].key && inv.slots()[0].count == before[0].count,
                     "rejected creative item changed inventory");
        const auto key = inv.slots()[0].key;
        const int count = inv.slots()[0].count;
        require(!inv.selectHotbarItem(0, i, spec.maxStack + 1), "accepted oversized stack");
        require(inv.slots()[0].key == key && inv.slots()[0].count == count, "rejected stack changed inventory");
    }
    // Every registered stairs direction/half is selected through the compatibility
    // entry point, but must become the same source item, aux=0, defaultData=0.
    for (int i = 0; i < sourceitems::blockStateCount(); ++i) {
        const auto& s = sourceitems::blockStates()[i];
        if (!stairs(s.tileId)) continue;
        require(inv.selectHotbarBlock(0, s.renderCatalogKey, 7), "stairs state selection rejected");
        selected(inv, s.tileId, 0, 7);
    }
    for (int tile : {44, 64, 71}) {
        const int source = tile == 44 ? 44 : tile == 64 ? 324 : 330;
        for (const auto& s : sourceitems::kBlockStates) {
            if (s.tileId != tile || (tile == 44 && (s.data & 7) != 0)) continue;
            require(inv.selectHotbarBlock(0, s.renderIndex, 1), "half/direction selection rejected");
            selected(inv, source, 0, 1);
        }
    }
    const auto before = inv.slots()[0];
    require(!inv.selectHotbarItem(0, itemIndex(324), 2), "door accepted count two");
    require(!inv.selectHotbarItem(0, -1, 1) && !inv.selectHotbarItem(0, 126, 1), "invalid item index accepted");
    require(inv.slots()[0].key == before.key && inv.slots()[0].count == before.count, "rejected action mutated slot");
    require(!mcworld::placementItem(53, 4) && !mcworld::placementItem(44, 8) && !mcworld::placementItem(324, 8),
            "world half/orientation accepted as source aux");
    // Final persistent hotbar deliberately uses noncanonical state inputs.
    const int render[] = {stateIndex(53, 7), stateIndex(44, 8), stateIndex(64, 11),
                          stateIndex(35, 14), stateIndex(5, 2), stateIndex(17, 9)};
    for (int slot = 0; slot < 6; ++slot)
        require(inv.selectHotbarBlock(slot, render[slot], slot == 2 ? 1 : 7), "persistent hotbar selection failed");
    require(inv.select(2), "select door"); selected(inv, 324, 0, 1);
    std::printf("{\"event\":\"creative-selection\",\"ok\":true,\"allItems\":126,\"canonicalStates\":true}\n");
}
// Tiny pure planner fixture: one support cube, air elsewhere. Not a world/save
// fixture: no terrain files are patched, and no hidden test mutators are needed.
mcworld::PlacementContext context(int face, float yaw, float click) {
    mcworld::PlacementContext c;
    const uint16_t stone = uint16_t(stateIndex(1, 0) + 1);
    c.hit = {true, 10, 100, 12, face, stone, 2};
    c.yaw = yaw; c.clickHeight = click;
    c.playerX = 10.5f; c.playerY = 99; c.playerZ = 8.5f;
    c.blockAt = [stone](int x, int y, int z) { return x == 10 && y == 100 && z == 12 ? stone : uint16_t(0); };
    c.available = [](int x, int y, int z) { return x >= 0 && x < 32 && y > 0 && y < 256 && z >= 0 && z < 32; };
    return c;
}
std::vector<mcworld::PlacementChange> plan(int id, int face, float yaw, float click) {
    const auto* spec = mcworld::placementItem(id, 0);
    require(spec != nullptr, "planner source item missing");
    std::vector<mcworld::PlacementChange> changes;
    require(mcworld::planItemPlacement(*spec, context(face, yaw, click), changes), "placement plan rejected");
    require(!changes.empty(), "successful placement plan is empty");
    return changes;
}
void plannedState(const mcworld::PlacementChange& change, int tile, int data, int y, int z) {
    require(change.x == 10 && change.y == y && change.z == z && change.renderIndex == stateIndex(tile, data),
            "unexpected planned cell/metadata");
    require(sourceitems::itemForBlockState(change.renderIndex) == itemIndex(tile == 64 ? 324 : tile),
            "planned state has wrong source identity");
}
void placementPlans() {
    stage = "placement-plans";
    const float yaws[] = {0, 90, 180, -90};
    const int directions[] = {2, 0, 3, 1};
    unsigned mask = 0;
    for (int n = 0; n < 4; ++n) {
        auto lower = plan(53, 2, yaws[n], .25f);
        auto upper = plan(53, 2, yaws[n], .75f);
        require(lower.size() == 1 && upper.size() == 1, "stairs must plan one cell");
        plannedState(lower[0], 53, directions[n], 100, 11);
        plannedState(upper[0], 53, directions[n] | 4, 100, 11);
        mask |= 1u << directions[n];
    }
    require(mask == 15, "same stairs item did not produce four yaw directions");
    for (int face : {0, 1, 2}) {
        for (float click : {.25f, .75f}) {
            const int data = face == 0 || (face == 2 && click > .5f) ? 8 : 0;
            const auto slab = plan(44, face, 0, click);
            require(slab.size() == 1, "slab must plan one cell");
            plannedState(slab[0], 44, data, face == 0 ? 99 : face == 1 ? 101 : 100, face == 2 ? 11 : 12);
        }
    }
    const int doorDirections[] = {1, 0, 3, 2};
    for (int n = 0; n < 4; ++n) {
        const auto door = plan(324, 1, yaws[n], 1);
        require(door.size() == 2, "door must plan two cells");
        plannedState(door[0], 64, doorDirections[n], 101, 12);
        plannedState(door[1], 64, 8, 102, 12);
    }
    auto blocked = context(1, 0, 1);
    const auto originalRead = blocked.blockAt;
    blocked.blockAt = [originalRead](int x, int y, int z) {
        return x == 10 && y == 102 && z == 12 ? uint16_t(stateIndex(1, 0) + 1) : originalRead(x, y, z);
    };
    std::vector<mcworld::PlacementChange> rejected {{0, 0, 0, 0}};
    require(!mcworld::planItemPlacement(*mcworld::placementItem(324, 0), blocked, rejected) && rejected.empty(),
            "obstructed upper door leaked a partial plan");
    std::printf("{\"event\":\"placement-plans\",\"ok\":true,\"stairsData\":[2,0,3,1],\"upperBit\":4,\"slabHalves\":true,\"doorCells\":2}\n");
}
void materials(mcworld::World& world) {
    stage = "material-inventory";
    mcinventory::Inventory inv; inv.open(world);
    for (int id : {35, 5, 17}) for (int aux = 0; aux < (id == 35 ? 16 : 4); ++aux)
        require(inv.add(stateIndex(id, aux), 1), "adding material variant failed");
    int slot = 0;
    for (int id : {35, 5, 17}) for (int aux = 0; aux < (id == 35 ? 16 : 4); ++aux, ++slot)
        require(inv.slots()[slot].key == item(id, aux).key && inv.slots()[slot].count == 1,
                "distinct material inventory slots collapsed");
    canonical(inv);
    // Deliberately not saved: persistent inventory is tested independently below.
    std::printf("{\"event\":\"material-inventory\",\"ok\":true,\"distinctSlots\":%d}\n", slot);
}
void render(const mcinventory::Inventory& inv) {
    stage = "inventory-renderer";
    constexpr int w = 320, h = 240;
    constexpr uint32_t background = 0xff304050u;
    std::vector<uint32_t> pixels(size_t(w) * h, background);
    mcinventory::renderInventoryOverlay(pixels.data(), w, h, inv, true);
    size_t changed = 0;
    for (uint32_t pixel : pixels) changed += pixel != background;
    require(changed > 100, "inventory renderer produced no overlay");
    std::printf("{\"event\":\"inventory-renderer\",\"ok\":true,\"changedPixels\":%zu}\n", changed);
}
std::vector<mcworld::PlacementChange> naturalPlacement(mcworld::World& world) {
    stage = "natural-placement";
    // Bounded camera sweep from the untouched natural spawn; no teleport, mining,
    // artificial supports, file fixtures or movement needed to force a placement.
    constexpr double radians = .017453292519943295769;
    for (float pitch : {-30.f, -45.f, -60.f, 0.f}) for (int yaw = 0; yaw < 360; yaw += 45) {
        const auto hit = world.pick(float(yaw), pitch);
        if (!hit.hit || hit.face < 0) continue;
        mcworld::PlacementContext c;
        c.hit = hit; c.yaw = float(yaw);
        const double hitY = double(world.playerY()) + 1.62 + std::sin(double(pitch) * radians) * hit.distance - hit.y;
        c.clickHeight = hit.face == 0 ? 0 : hit.face == 1 ? 1 : float(std::fmax(0., std::fmin(1., hitY)));
        c.playerX = world.playerX(); c.playerY = world.playerY(); c.playerZ = world.playerZ();
        c.blockAt = [&world](int x, int y, int z) { return world.blockAt(x, y, z); };
        c.available = [&world](int x, int y, int z) {
            return x >= 0 && x < world.width() && y > 0 && y < world.height() &&
                   z >= 0 && z < world.depth() && world.chunkLoaded(x, z);
        };
        std::vector<mcworld::PlacementChange> changes;
        if (!mcworld::planItemPlacement(*mcworld::placementItem(53, 0), c, changes) || changes.empty()) continue;
        bool overlap = false;
        for (const auto& change : changes) {
            overlap |= change.x + 1 > world.playerX() - .25f && change.x < world.playerX() + .25f &&
                       change.y + 1 > world.playerY() && change.y < world.playerY() + 1.8f &&
                       change.z + 1 > world.playerZ() - .25f && change.z < world.playerZ() + .25f;
        }
        if (overlap) continue;
        const auto revision = world.revision();
        require(world.placeItem(float(yaw), pitch, 53, 0), "suitable natural plan rejected by World::placeItem");
        require(world.revision() != revision, "placement did not advance terrain revision");
        for (const auto& change : changes)
            require(world.blockAt(change.x, change.y, change.z) == change.renderIndex + 1, "placed metadata differs from plan");
        std::printf("{\"event\":\"natural-placement\",\"ok\":true,\"item\":53,\"yaw\":%d,\"pitch\":%.0f,\"cells\":%zu}\n",
                    yaw, double(pitch), changes.size());
        return changes;
    }
    // This optional branch is visible; no mandatory check/exception is skipped.
    std::printf("{\"event\":\"natural-placement\",\"status\":\"not-applicable\",\"reason\":\"no suitable target in bounded natural-spawn sweep\"}\n");
    return {};
}
void roundtrip(const std::string& root) {
    stage = "create-world";
    const uint32_t seed = 12345;
    std::string path;
    std::array<mcinventory::Slot, mcinventory::Inventory::MainSlots> expected;
    std::vector<mcworld::PlacementChange> placed;
    float px = 0, py = 0, pz = 0;
    {
        mcworld::World world;
        world.create(seed, root, true, 864, false, false);
        require(world.loaded() && world.originalGeneration() && world.width() == 864, "original world create failed");
        path = world.path();
        require(path.compare(0, root.size() + 1, root + "/") == 0, "created world escaped temporary root");
        std::printf("{\"event\":\"world-created\",\"seed\":%u,\"size\":864}\n", seed);
        materials(world);
        mcinventory::Inventory inv; inv.open(world);
        creativeSelection(inv);
        render(inv);
        placed = naturalPlacement(world);
        canonical(inv); selected(inv, 324, 0, 1); // Placement metadata never enters the inventory.
        expected = inv.slots();
        px = world.playerX(); py = world.playerY(); pz = world.playerZ();
        stage = "save-world-inventory";
        world.save(); inv.save(world);
    } // Reopen fresh objects, not an in-memory alias of the created world.
    stage = "reopen-world-inventory";
    mcworld::World reopened; reopened.open(path, root);
    require(reopened.loaded() && reopened.originalGeneration() && reopened.seed() == seed && reopened.width() == 864 &&
            reopened.playerX() == px && reopened.playerY() == py && reopened.playerZ() == pz, "world reopen identity/player mismatch");
    mcinventory::Inventory restored; restored.open(reopened);
    selected(restored, 324, 0, 1);
    require(restored.selectedSlot() == 2, "selected hotbar slot did not persist");
    for (size_t i = 0; i < expected.size(); ++i)
        require(restored.slots()[i].key == expected[i].key && restored.slots()[i].count == expected[i].count,
                "inventory save/reopen changed canonical identity/count");
    for (const auto& change : placed)
        require(reopened.blockAt(change.x, change.y, change.z) == change.renderIndex + 1,
                "placed world metadata did not survive save/reopen");
    render(restored);
    std::printf("{\"event\":\"save-reopen\",\"ok\":true,\"selected\":\"item:324:0\",\"selectedSlot\":2,\"placedCellsVerified\":%zu}\n", placed.size());
}
} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    TemporaryRoot temporary;
    try {
        catalog(); placementPlans();
        stage = "mkdtemp"; temporary.create();
        std::printf("{\"event\":\"temporary-root\",\"path\":"); jsonString(temporary.path); std::printf("}\n");
        roundtrip(temporary.path);
        stage = "cleanup";
        require(temporary.cleanup(), "temporary root cleanup failed");
        std::printf("{\"event\":\"result\",\"ok\":true}\n");
        return 0;
    } catch (const std::exception& error) {
        std::printf("{\"event\":\"result\",\"ok\":false,\"stage\":"); jsonString(stage);
        std::printf(",\"error\":"); jsonString(error.what()); std::printf("}\n");
    } catch (...) {
        std::printf("{\"event\":\"result\",\"ok\":false,\"stage\":"); jsonString(stage);
        std::printf(",\"error\":\"unknown exception\"}\n");
    }
    return 1;
}
