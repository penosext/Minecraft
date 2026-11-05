// Host test for crafting/chest/furnace containers through mccontainers::Containers
// (the same API JSMinecraftRenderer exposes), GameActions tile hooks, the
// smelting tables and the native container GUI layout at 960x266.
#define MCWORLD_INTERACTION_FIXTURES_ONLY
#include "world-interaction-test.cpp"
#include "BlockEntities.hpp"
#include "GameActions.hpp"
#include "Inventory.hpp"
#include "InventoryRenderer.hpp"
#include "Scene.hpp"
#include "ItemCatalog.hpp"
#include "Smelting.hpp"

namespace {
using mccontainers::Containers;
using mcinventory::Inventory;
using mcinventory::PanelKind;
using CR = Containers::CraftResult;

constexpr int CX = 100, CY = 200;
struct Fixture {
    Temp root;
    std::string path;
    World world;
    Inventory inventory;
    Containers containers;
    // Stone under the player; workbench +Z, chest -Z, furnace +X.
    Fixture() {
        World fixture; fixture.create(12345, root.path, true, 864, false, false);
        path = fixture.path();
        originalFixture(path, {{CX, CY, 100, blockId("stone")}, {CX, CY, 102, blockId("workBench")},
                               {CX, CY, 98, blockId("chest_data_2")}, {102, CY, 100, blockId("furnace")}});
        player(path, 100.5f, CY + 1, 100.5f);
        reopen();
    }
    void reopen() {
        world = World(); world.open(path, root.path);
        inventory = Inventory(); inventory.open(world);
        containers = Containers(); containers.open(world);
    }
    void saveAll() { world.save(); inventory.save(world); containers.save(world); }
    int count(const std::string& key) const {
        int total = 0;
        for (const auto& slot : inventory.slots()) if (slot.key == key) total += slot.count;
        return total;
    }
    int slotOf(const std::string& key) const {
        for (int i = 0; i < 36; ++i) if (inventory.slots()[i].key == key) return i;
        assert(!"item not in inventory");
        return -1;
    }
    void put(const std::string& key, int containerSlot, int amount = 1) {
        assert(containers.transfer(world, inventory, slotOf(key), containerSlot, amount, true));
    }
};

Inventory filled(Inventory copy) {
    for (int i = 0; i < 36; ++i) if (!copy.slots()[i].count) assert(copy.addItem(1, 0, 64));
    return copy;
}

void inventoryGridCraftsAndReturnsOnClose() {
    Fixture f;
    assert(f.inventory.addItem(17, 0, 3)); // oak logs
    assert(f.containers.openInventory(f.world) && f.containers.screen() == Containers::Screen::Inventory);
    f.put("item:17:0", 0);
    auto match = f.containers.preview();
    assert(match.matched && match.productKey == "item:5:0" && match.count == 4);
    assert(f.containers.view().result.key == "item:5:0" && f.containers.view().result.count == 4);
    assert(f.containers.craft(f.world, f.inventory) == CR::Ok);
    assert(f.count("item:5:0") == 4 && f.count("item:17:0") == 2);
    assert(f.containers.view().slots[0].count == 0 && !f.containers.preview().matched);
    // Two planks in the left column (2x2 slots 0 and 2) -> 4 sticks.
    f.put("item:5:0", 0); f.put("item:5:0", 2);
    match = f.containers.preview();
    assert(match.matched && match.productKey == "item:280:0" && match.count == 4);
    // Same shape in the right column also matches (ShapedRecipy offset scan).
    assert(f.containers.quickMove(f.world, f.inventory, 0) && f.containers.quickMove(f.world, f.inventory, 2));
    f.put("item:5:0", 1); f.put("item:5:0", 3);
    assert(f.containers.preview().productKey == "item:280:0");
    // 2x2 planks -> workbench.
    f.put("item:5:0", 0); f.put("item:5:0", 2);
    assert(f.containers.preview().productKey == "item:58:0");
    assert(f.containers.craft(f.world, f.inventory) == CR::Ok && f.count("item:58:0") == 1);
    assert(f.count("item:5:0") == 0);
    // Grid leftovers return on close (InventoryMenu::removed).
    f.put("item:17:0", 3, 2);
    assert(f.count("item:17:0") == 0);
    assert(f.containers.close(f.world, f.inventory) == 0);
    assert(f.count("item:17:0") == 2 && !f.containers.opened());
    assert(f.containers.craft(f.world, f.inventory) == CR::NoPanel);
    // A grid stack that cannot return persists in the v2 trailer and comes back.
    assert(f.containers.openInventory(f.world));
    f.put("item:17:0", 0, 2);
    Inventory full = filled(f.inventory);
    assert(f.containers.close(f.world, full) == 2);
    f.saveAll(); f.reopen();
    assert(f.containers.openInventory(f.world) && f.containers.view().slots[0].count == 2);
    assert(f.containers.close(f.world, f.inventory) == 0 && f.count("item:17:0") == 2);
}

void workbenchShapedCraftAndInventoryFull() {
    Fixture f;
    assert(f.inventory.addItem(5, 0, 3) && f.inventory.addItem(280, 0, 3));
    assert(f.containers.interact(f.world, CX, CY, 102) && f.containers.screen() == Containers::Screen::Workbench);
    assert(!f.containers.openInventory(f.world)); // one panel at a time
    assert(!f.containers.interact(f.world, CX, CY, 98));
    f.put("item:5:0", 0); f.put("item:5:0", 1); f.put("item:5:0", 2);
    f.put("item:280:0", 4); f.put("item:280:0", 7);
    assert(f.containers.preview().productKey == "item:270:0"); // wooden pickaxe
    // The product does not fit: nothing is consumed (no player->drop).
    Inventory full = filled(f.inventory);
    assert(f.containers.craft(f.world, full) == CR::InventoryFull);
    assert(f.containers.view().slots[0].count == 1);
    assert(f.containers.craft(f.world, f.inventory) == CR::Ok && f.count("item:270:0") == 1);
    for (const auto& slot : f.containers.view().slots) assert(slot.count == 0);
    assert(f.containers.craft(f.world, f.inventory) == CR::NoRecipe);
    assert(f.containers.close(f.world, f.inventory) == 0);
    f.saveAll(); f.reopen();
    // A leftover that cannot return stays in the persisted bench. Breaking is
    // allowed now (removed() hands the leftover back to drop as an item).
    assert(f.containers.interact(f.world, CX, CY, 102));
    f.put("item:280:0", 8);
    full = filled(f.inventory);
    assert(f.containers.close(f.world, full) == 1);
    assert(!f.containers.opened() && f.containers.canBreak(CX, CY, 102));
    f.saveAll(); f.reopen();
    assert(f.containers.interact(f.world, CX, CY, 102));
    assert(f.containers.view().slots[8].key == "item:280:0");
    assert(f.containers.close(f.world, f.inventory) == 0 && f.count("item:280:0") == 1);
    assert(f.containers.canBreak(CX, CY, 102));
}

void chestPersistsAndBreakDropsContents() {
    Fixture f;
    mcscene::Scene scene; scene.open(f.world);
    assert(f.inventory.addItem(4, 0, 40) && f.inventory.addItem(265, 0, 5));
    mcgame::GameActions actions;
    // Same wiring as JSMinecraftRenderer: contents and drops become item entities.
    actions.setTileHooks({[&](int x, int y, int z) { return f.containers.canBreak(x, y, z); },
                          [&](int x, int y, int z) { scene.dropContents(x, y, z, f.containers.removed(x, y, z)); },
                          [&](int x, int y, int z) { return f.containers.interact(f.world, x, y, z); },
                          [&](int x, int y, int z, int item, int aux, int count) { scene.popResource(x, y, z, item, aux, count); }});
    // Right click (GameActions::use) opens the chest through the hook before
    // any placement, so the held cobblestone is not consumed.
    const int before = f.count("item:4:0");
    assert(actions.use(f.world, f.inventory, 180, -45));
    assert(f.containers.screen() == Containers::Screen::Chest && f.count("item:4:0") == before);
    assert(f.containers.view().slots.size() == 27);
    f.put("item:4:0", 26, 64);
    f.put("item:265:0", 0, 5);
    assert(f.count("item:4:0") == 0 && f.count("item:265:0") == 0);
    assert(f.containers.transfer(f.world, f.inventory, 0, 0, 2, false) && f.count("item:265:0") == 2);
    assert(f.containers.close(f.world, f.inventory) == 0);
    assert(f.containers.view().kind == PanelKind::None);
    f.saveAll(); f.reopen();
    assert(f.containers.interact(f.world, CX, CY, 98));
    auto slots = f.containers.view().slots;
    assert(slots[0].key == "item:265:0" && slots[0].count == 3 && slots[26].key == "item:4:0" && slots[26].count == 40);
    assert(f.containers.infoJson(f.inventory).find("\"type\":\"chest\"") != std::string::npos);
    assert(f.containers.close(f.world, f.inventory) == 0);
    // The open panel's own block is the only one that refuses a break.
    assert(f.containers.interact(f.world, CX, CY, 98));
    const auto& refused = actions.tickMining(f.world, f.inventory, 180, -45, true, .05f);
    assert(refused.protectedBlock && !refused.broken && !f.containers.canBreak(CX, CY, 98));
    assert(actions.miningInfoJson().find("\"protectedBlock\":true") != std::string::npos);
    f.containers.close(f.world, f.inventory);
    actions.resetMining();
    // A FULL chest breaks (creative here) and its contents fall out as item
    // entities: 3 iron + 40 cobblestone, split into 10..30 stacks
    // (ChestTile::onRemove); creative yields no chest item itself.
    bool broke = false;
    for (int i = 0; i < 40 && !broke; ++i) broke = actions.tickMining(f.world, f.inventory, 180, -45, true, .05f).broken;
    assert(broke && f.world.blockAt(CX, CY, 98) == 0);
    int iron = 0, cobble = 0;
    for (const auto& e : scene.items()) {
        assert(e.count >= 1 && e.count <= 30 && e.pickupDelay == 0);
        assert(e.x >= CX + .1 && e.x <= CX + .9 && e.z >= 98.1 && e.z <= 98.9);
        if (e.itemId == 265) iron += e.count; else if (e.itemId == 4) cobble += e.count; else assert(!"unexpected drop");
    }
    assert(iron == 3 && cobble == 40 && scene.items().size() >= 3);
    assert(f.count("item:4:0") == 0 && f.count("item:265:0") == 2); // inventory untouched until pickup
    // Its record is gone: the contents cannot come back on reopen (no duplicate).
    f.saveAll(); scene.save(f.world); f.reopen();
    assert(!f.containers.interact(f.world, CX, CY, 98)); // air now
    mcscene::Scene reopened; reopened.open(f.world);
    assert(reopened.items().size() == scene.items().size());
}

void furnaceSmeltsWithSourceTimings() {
    Fixture f;
    assert(f.inventory.addItem(15, 0, 2) && f.inventory.addItem(5, 0, 2) && f.inventory.addItem(17, 2, 2));
    assert(f.containers.interact(f.world, 102, CY, 100) && f.containers.screen() == Containers::Screen::Furnace);
    f.put("item:15:0", 0, 2);
    f.put("item:5:0", 1, 1);
    // Output is extract-only (FurnaceResultSlot::mayPlace).
    assert(!f.containers.transfer(f.world, f.inventory, f.slotOf("item:17:2"), 2, 1, true));
    // Ticking continues with the panel closed.
    f.containers.close(f.world, f.inventory);
    auto tick = [&](int ticks) { for (int i = 0; i < ticks; ++i) f.containers.update(f.world, .05f); };
    tick(1);
    assert(f.containers.interact(f.world, 102, CY, 100));
    auto view = f.containers.view();
    // One plank = 300 ticks (Material::wood, 200*3/2), consumed on ignition.
    assert(view.lit && view.slots[1].count == 0 && view.burnProgress == 0);
    tick(199); // cook reaches 200 on tick 200 -> one ingot.
    view = f.containers.view();
    assert(view.slots[2].key == "item:265:0" && view.slots[2].count == 1 && view.slots[0].count == 1);
    tick(101); // fuel ends on tick 301 with no refill: cook resets to 0.
    view = f.containers.view();
    assert(!view.lit && view.slots[2].count == 1 && view.slots[0].count == 1);
    assert(f.containers.infoJson(f.inventory).find("\"cookTicks\":0") != std::string::npos);
    f.put("item:17:2", 1, 1); // birch log fuel, 300 ticks
    tick(150);
    view = f.containers.view();
    assert(view.lit && view.burnProgress == 18 && view.litProgress == 6);
    assert(!f.containers.canBreak(102, CY, 100)); // its panel is open (a lit furnace may break otherwise)
    // State survives save/reopen (FurnaceTileEntity BurnTime/CookTime).
    f.containers.close(f.world, f.inventory);
    f.saveAll(); f.reopen();
    assert(f.containers.interact(f.world, 102, CY, 100));
    assert(f.containers.infoJson(f.inventory).find("\"burnRemaining\":151,\"burnTotal\":300,\"cookTicks\":150") != std::string::npos);
    tick(50);
    view = f.containers.view();
    assert(view.slots[2].count == 2 && view.slots[0].count == 0);
    assert(f.containers.quickMove(f.world, f.inventory, 2) && f.count("item:265:0") == 2);
    // Logs -> charcoal only when the catalog knows coal; never invent an item.
    tick(200); // burn out the leftover fuel
    f.put("item:17:2", 0, 1); f.put("item:5:0", 1, 1);
    tick(201);
    view = f.containers.view();
    const bool coalKnown = sourceitems::itemIndexForIdAux(263, 1) >= 0;
    assert(coalKnown); // merged catalog has coal/charcoal; logs must really make charcoal
    assert((view.slots[2].key == "item:263:1") == coalKnown);
    assert(view.slots[0].count == (coalKnown ? 0 : 1));
}

void smeltingTables() {
    using namespace smelting;
    assert(kBurnInterval == 200);
    assert(burnDuration(263, 0) == 1600 && burnDuration(263, 1) == 1600);
    assert(burnDuration(5, 3) == 300 && burnDuration(17, 0) == 300 && burnDuration(58, 0) == 300);
    assert(burnDuration(126, 0) == 150 && burnDuration(173, 0) == 16000 && burnDuration(327, 0) == 20000);
    assert(burnDuration(280, 0) == 100 && burnDuration(6, 0) == 100 && burnDuration(369, 0) == 2400);
    assert(burnDuration(270, 0) == 200 && burnDuration(274, 0) == 0); // wooden pickaxe yes, stone no
    assert(burnDuration(65, 0) == 0 && burnDuration(44, 2) == 0 && burnDuration(1, 0) == 0);
    auto r = smeltResult(15); assert(r.ok && r.itemId == 265 && r.count == 1);
    r = smeltResult(17); assert(r.ok && r.itemId == 263 && r.aux == 1);
    r = smeltResult(4); assert(r.ok && r.itemId == 1);
    r = smeltResult(12); assert(r.ok && r.itemId == 20);
    r = smeltResult(319); assert(r.ok && r.itemId == 320);
    assert(!smeltResult(1).ok && !smeltResult(265).ok);
    assert(recipeInputCount() == 20);
}

bool overlaps(const mcinventory::Rect& a, const mcinventory::Rect& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

void layoutAt960x266() {
    using mcinventory::ContainerLayout;
    for (auto kind : {PanelKind::Inventory, PanelKind::Workbench, PanelKind::Chest, PanelKind::Furnace}) {
        const auto layout = mcinventory::containerLayout(960, 266, kind);
        assert(layout.scale == 1.5f);
        assert(layout.panel.x >= 0 && layout.panel.y >= 0 && layout.panel.x + layout.panel.w <= 960 &&
               layout.panel.y + layout.panel.h <= 266);
        std::vector<std::pair<mcinventory::Rect, int>> hits;
        for (int i = 0; i < 36; ++i) hits.push_back({layout.inventoryHits[i], i});
        for (size_t i = 0; i < layout.slotHits.size(); ++i) hits.push_back({layout.slotHits[i], ContainerLayout::kSlotBase + int(i)});
        if (layout.hasResult) hits.push_back({layout.resultHit, ContainerLayout::kResult});
        const size_t expected = 36 + (kind == PanelKind::Inventory ? 5 : kind == PanelKind::Workbench ? 10 :
                                      kind == PanelKind::Chest ? 27 : 3);
        assert(hits.size() == expected);
        for (size_t i = 0; i < hits.size(); ++i) {
            const auto& r = hits[i].first;
            assert(r.x >= layout.panel.x && r.y >= layout.panel.y && r.x + r.w <= layout.panel.x + layout.panel.w &&
                   r.y + r.h <= layout.panel.y + layout.panel.h);
            for (size_t j = i + 1; j < hits.size(); ++j) assert(!overlaps(r, hits[j].first));
            assert(layout.hitTest(r.x + r.w / 2, r.y + r.h / 2) == hits[i].second);
        }
        if (kind == PanelKind::Inventory) {
            // Same panel/rows as the plain inventory; the grid never touches the figure.
            const auto inv = mcinventory::inventoryLayout(960, 266);
            assert(inv.panel.x == layout.panel.x && inv.panel.y == layout.panel.y);
            for (int i = 0; i < 36; ++i) assert(inv.tileHitSlots[i].x == layout.inventoryHits[i].x &&
                                                inv.tileHitSlots[i].y == layout.inventoryHits[i].y);
            for (const auto& r : layout.slotHits) assert(!overlaps(r, inv.playerView));
            assert(!overlaps(layout.resultHit, inv.playerView));
        }
    }
    const auto chest = mcinventory::containerLayout(960, 266, PanelKind::Chest);
    // 168 source units at 1.5x = 252 px, centred in 266 px.
    assert(chest.panel.h == 252 && chest.panel.y == 7 && chest.panel.x == 348 && chest.panel.w == 264);
    const auto furnace = mcinventory::containerLayout(960, 266, PanelKind::Furnace);
    assert(furnace.panel.x == 348 && furnace.panel.y == 8 && furnace.panel.w == 264 && furnace.panel.h == 249);
    // The Vue side column (index.less .container-tools/.inventory-tools) starts at x=620.
    for (auto kind : {PanelKind::Inventory, PanelKind::Workbench, PanelKind::Chest, PanelKind::Furnace})
        assert(mcinventory::containerLayout(960, 266, kind).panel.x + mcinventory::containerLayout(960, 266, kind).panel.w <= 620);
    std::vector<uint32_t> buffer(960 * 266, 0xff336699u);
    Fixture f;
    for (auto kind : {PanelKind::Inventory, PanelKind::Workbench, PanelKind::Chest, PanelKind::Furnace}) {
        mcinventory::PanelView view; view.kind = kind;
        view.slots.resize(kind == PanelKind::Inventory ? 4 : kind == PanelKind::Workbench ? 9 : kind == PanelKind::Chest ? 27 : 3);
        view.slots[0] = {"item:4:0", 12, 0}; view.lit = true; view.litProgress = 12; view.burnProgress = 24;
        mcinventory::renderContainerOverlay(buffer.data(), 960, 266, f.inventory, view);
    }
}
} // namespace

int main() {
    smeltingTables();
    layoutAt960x266();
    inventoryGridCraftsAndReturnsOnClose();
    workbenchShapedCraftAndInventoryFull();
    chestPersistsAndBreakDropsContents();
    furnaceSmeltsWithSourceTimings();
    std::cout << "containers-test: 2x2/3x3 crafting, close-return, chest persistence/break drops contents, furnace ticks/fuel/persistence, layout passed\n";
}
