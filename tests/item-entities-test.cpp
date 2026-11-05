// Dropped item entities: ItemEntity physics/merge/despawn/pickup, the bounded
// store, items.bin persistence and old-save compatibility, survival mining
// drops (DoTileDrops, creative), death drops (crash-safe ordering) and the
// world-view renderer (smoke + cost at 960x266).
#define main worldRegressionMain
#include "world-test.cpp"
#undef main
#include "GameActions.hpp"
#include "Inventory.hpp"
#include "ItemCatalog.hpp"
#include "ItemEntities.hpp"
#include "ItemEntityRenderer.hpp"
#include "PlayerSurvival.hpp"
#include "Scene.hpp"
#include "SurvivalFrame.hpp"
#include "WorldRenderer.hpp"

#include <chrono>
#include <cstdio>

using mcscene::ItemEntities;
using mcscene::ItemEntity;

namespace {
int passed = 0;
void ok(const char* name) { ++passed; std::printf("PASS %s\n", name); }

// Flat two-layer stone floor at y = 8..9 (surface y = 10); optional extra blocks.
struct Fixture {
    Temp root;
    World world;
    std::string path;
    Bytes flat;
    explicit Fixture(const std::vector<std::array<int, 4>>& extra = {}) {
        World original; original.create(902, root.path, false); path = original.path();
        flat = read(path + "/chunks.bin");
        std::fill(flat.begin() + 40, flat.end(), 0);
        for (int z = 0; z < mcworld::Depth; ++z) for (int x = 0; x < mcworld::Width; ++x)
            for (int y : {8, 9}) block(flat, x, y, z, blockId("stone"));
        for (const auto& b : extra) block(flat, b[0], b[1], b[2], uint16_t(b[3]));
        fs::remove(path + "/edits.bin"); synchronize(path, flat); player(path, 32.5f, 10, 32.5f);
        world = World(); world.open(path, root.path);
    }
};
ItemEntity item(int id, int count, double x, double y, double z) {
    ItemEntity e; e.itemId = id; e.aux = 0; e.count = count; e.x = x; e.y = y; e.z = z; return e;
}
int itemIndex(int id, int aux = 0) { return sourceitems::itemIndexForIdAux(id, aux); }
int countOf(const mcinventory::Inventory& inv, const std::string& key) {
    int total = 0; for (const auto& s : inv.slots()) if (s.key == key) total += s.count; return total;
}
mcinventory::Inventory survivalInventory(const World& world) {
    mcinventory::Inventory inv; inv.open(world);
    assert(inv.setGameMode(mcinventory::GameMode::Survival));
    return inv;
}

void physics() {
    Fixture f;
    ItemEntities store; store.reset(1);
    // Falls under gravity .04/tick and drag .98, lands exactly on the floor.
    assert(store.add(item(4, 1, 20.5, 15, 20.5)));
    store.tick(f.world);
    assert(std::abs(store.items()[0].yd - float(-.04 * .98)) < 1e-6f && store.items()[0].y < 15);
    for (int i = 0; i < 60; ++i) { store.tick(f.world); assert(store.items()[0].y >= 10 - 1e-9); }
    assert(std::abs(store.items()[0].y - 10) < 1e-9 && store.items()[0].onGround);
    // Terminal-speed fall cannot tunnel into the floor.
    store.reset(1);
    ItemEntity fast = item(4, 1, 21.5, 30, 21.5); fast.yd = -20;
    assert(store.add(fast));
    store.tick(f.world);
    assert(std::abs(store.items()[0].y - 10) < 1e-9);
    // Ground friction .6 * .98 per tick (stone friction .6).
    store.reset(1);
    ItemEntity slide = item(4, 1, 22.5, 10, 22.5); slide.xd = .3f;
    assert(store.add(slide));
    store.tick(f.world); // lands (onGround), friction applies this tick
    const float before = store.items()[0].xd;
    store.tick(f.world);
    assert(std::abs(store.items()[0].xd - before * .6f * .98f) < 1e-6f);
    for (int i = 0; i < 100; ++i) store.tick(f.world);
    assert(std::abs(store.items()[0].xd) < 1e-4f && store.items()[0].x > 22.8 && store.items()[0].x < 24);
    // An item inside the floor is pushed up (Entity::checkInTile) and ends on top.
    store.reset(1);
    assert(store.add(item(4, 1, 25.5, 9.3, 25.5)));
    for (int i = 0; i < 40; ++i) store.tick(f.world);
    assert(store.items()[0].y >= 10 - 1e-9 && store.items()[0].y < 10.5);
    ok("falls (gravity .04, drag .98), lands on the floor, no tunnelling, ground friction .588, pushed out of blocks");
}

void validation() {
    Fixture f;
    ItemEntities store; store.reset(1);
    assert(!store.add(item(4, 0, 20.5, 15, 20.5)));
    assert(!store.add(item(99999, 1, 20.5, 15, 20.5)));
    ItemEntity bad = item(4, 1, 20.5, 15, 20.5); bad.x = std::numeric_limits<double>::quiet_NaN();
    assert(!store.add(bad));
    ok("invalid identity/count/position rejected");
}

void mergeDespawnCap() {
    Fixture f;
    ItemEntities store; store.reset(1);
    assert(store.add(item(4, 1, 20.5, 10, 20.5)) && store.add(item(4, 2, 20.6, 10, 20.5)));
    assert(store.add(item(3, 1, 20.5, 10, 20.6)));                 // dirt never merges with cobble
    assert(store.add(item(4, 63, 20.4, 10, 20.4)));                // 63 + 3 > 64: no merge
    store.tick(f.world);
    int cobbleEntities = 0, cobble = 0;
    for (const auto& e : store.items()) if (e.itemId == 4) { ++cobbleEntities; cobble += e.count; }
    assert(cobble == 66 && cobbleEntities == 2 && store.size() == 3);
    // Damageable items (stack limit 1) never merge.
    store.reset(1);
    assert(store.add(item(278, 1, 20.5, 10, 20.5)) && store.add(item(278, 1, 20.5, 10, 20.5)));
    store.tick(f.world); assert(store.size() == 2);
    // Despawn at age 6000 (ItemEntity.h LIFETIME).
    store.reset(1);
    ItemEntity old = item(4, 1, 20.5, 10, 20.5); old.age = ItemEntities::Lifetime - 2;
    assert(store.add(old));
    store.tick(f.world); assert(store.size() == 1);
    store.tick(f.world); assert(store.size() == 0);
    // Bounded store: the oldest entity is removed first.
    store.reset(1);
    for (int i = 0; i < ItemEntities::Capacity + 2; ++i) {
        ItemEntity e = item(4, 1, 2.5 + (i % 60), 10, 2.5 + (i / 60) * 2); e.age = i;
        assert(store.add(e));
    }
    assert(store.size() == size_t(ItemEntities::Capacity) && store.items().front().age == 2);
    ok("merge same stacks within .5 (larger absorbs, <= max stack, tools never), despawn at 6000, cap 128 oldest-first");
}

void pickup() {
    Fixture f;
    mcscene::Scene scene; scene.open(f.world);
    auto inv = survivalInventory(f.world);
    // Nearly full: 35 slots of stone, one slot with 60 cobblestone.
    for (int i = 0; i < 35; ++i) assert(inv.addItem(1, 0, 64));
    assert(inv.addItem(4, 0, 60));
    ItemEntity e = item(4, 10, 32.5, 10, 33.5); e.pickupDelay = 3;
    assert(scene.itemStore().add(e));
    // Pickup delay: untouchable until throwTime reaches 0.
    assert(scene.pickupItems(f.world, inv, true) == 0);
    scene.update(f.world, .05f); scene.update(f.world, .05f);
    assert(scene.pickupItems(f.world, inv, true) == 0);
    scene.update(f.world, .05f);
    // Partial pickup: 4 fit, 6 remain in the world.
    assert(scene.pickupItems(f.world, inv, true) == 4);
    assert(countOf(inv, "item:4:0") == 64 && scene.items().size() == 1 && scene.items()[0].count == 6);
    assert(scene.pickupItems(f.world, inv, true) == 0 && scene.items()[0].count == 6);
    // Dead players do not pick up; far items are not touched.
    mcinventory::Inventory empty = survivalInventory(f.world);
    assert(scene.pickupItems(f.world, empty, false) == 0);
    assert(scene.itemStore().add(item(3, 1, 40.5, 10, 40.5)));
    assert(scene.pickupItems(f.world, empty, true) == 6 && countOf(empty, "item:4:0") == 6);
    assert(scene.items().size() == 1 && scene.items()[0].itemId == 3);
    // Creative instabuild: whatever does not fit is destroyed (Inventory.cpp:379).
    mcinventory::Inventory creative; creative.open(f.world);
    for (int i = 0; i < 36; ++i) assert(creative.addItem(1, 0, 64));
    assert(scene.itemStore().add(item(4, 5, 32.5, 10, 32.5)));
    assert(scene.pickupItems(f.world, creative, true) == 0 && scene.items().size() == 1);
    ok("pickup delay, partial pickup into a nearly full inventory, alive only, reach box, creative destroys overflow");
}

void persistence() {
    Fixture f;
    {
        mcscene::Scene scene; scene.open(f.world);
        scene.save(f.world);
        // Old save / nothing dropped: items.bin is not created, scene.bin stays v3.
        assert(!fs::exists(f.path + "/items.bin"));
        assert(read(f.path + "/scene.bin")[8] == 3);
        ItemEntity e = item(4, 7, 20.5, 10, 20.5); e.age = 123; e.pickupDelay = 4; e.bobOffs = 1.5f;
        assert(scene.itemStore().add(e));
        assert(scene.itemStore().add(item(278, 1, 21.5, 12, 21.5)));
        scene.save(f.world);
        assert(fs::exists(f.path + "/items.bin") && read(f.path + "/scene.bin")[8] == 3);
        assert(read(f.path + "/items.bin").size() == 48 + 2 * 72 + 4);
    }
    mcscene::Scene reopened; reopened.open(f.world);
    assert(reopened.items().size() == 2);
    const auto& a = reopened.items()[0];
    assert(a.itemId == 4 && a.count == 7 && a.age == 123 && a.pickupDelay == 4 && a.bobOffs == 1.5f && a.x == 20.5);
    assert(reopened.items()[1].itemId == 278 && reopened.items()[1].stackLimit == 1);
    assert(reopened.summaryJson().find("\"itemCount\":2") != std::string::npos);
    // Corruption is rejected without touching the live scene.
    const Bytes valid = read(f.path + "/items.bin");
    Bytes bad = valid; bad[60] ^= 1; write(f.path + "/items.bin", bad);
    rejects([&] { mcscene::Scene s; s.open(f.world); });
    bad = valid; u32(bad, 8, 2); u32(bad, bad.size() - 4, hash(bad, bad.size() - 4)); write(f.path + "/items.bin", bad);
    rejects([&] { mcscene::Scene s; s.open(f.world); });
    bad = valid; u32(bad, 48 + 8, 65); u32(bad, bad.size() - 4, hash(bad, bad.size() - 4)); write(f.path + "/items.bin", bad);
    rejects([&] { mcscene::Scene s; s.open(f.world); }); // count 65 > max stack
    write(f.path + "/items.bin", valid);
    // An externally replaced file is never overwritten by a pending change.
    mcscene::Scene owner; owner.open(f.world);
    assert(owner.itemStore().add(item(3, 1, 22.5, 10, 22.5)));
    bad = valid; u32(bad, 48 + 8, 8); u32(bad, bad.size() - 4, hash(bad, bad.size() - 4)); write(f.path + "/items.bin", bad);
    rejects([&] { owner.save(f.world); });
    write(f.path + "/items.bin", valid);
    owner.save(f.world);
    // Removing every item rewrites the file with zero records (not stale items).
    owner.itemStore().reset(f.world.seed()); owner.save(f.world);
    mcscene::Scene emptied; emptied.open(f.world); assert(emptied.items().empty());
    ok("items.bin v1 round trip, scene.bin stays v3, missing file = old save, corrupt/unknown/replaced rejected");
}

mcgame::GameActions miningActions(mcscene::Scene& scene) {
    mcgame::GameActions actions;
    actions.setTileHooks({nullptr, nullptr, nullptr,
                          [&scene](int x, int y, int z, int id, int aux, int count) { scene.popResource(x, y, z, id, aux, count); }});
    return actions;
}
bool mineDown(mcgame::GameActions& actions, World& world, mcinventory::Inventory& inv) {
    for (int i = 0; i < 600; ++i) if (actions.tickMining(world, inv, 0, -90, true, .05f).broken) return true;
    return false;
}

void miningDrops() {
    {
        Fixture f; mcscene::Scene scene; scene.open(f.world);
        mcinventory::Inventory inv; inv.open(f.world);
        assert(inv.selectHotbarItem(0, itemIndex(278), 1) && inv.select(0));
        assert(inv.setGameMode(mcinventory::GameMode::Survival));
        auto actions = miningActions(scene);
        assert(mineDown(actions, f.world, inv));
        assert(f.world.blockAt(32, 9, 32) == 0);
        // One cobblestone entity in the mined cell; inventory unchanged except wear.
        assert(scene.items().size() == 1);
        const auto& e = scene.items()[0];
        assert(e.itemId == 4 && e.count == 1 && e.pickupDelay == ItemEntities::BlockDropDelay);
        assert(e.x >= 32.15 && e.x <= 32.85 && e.z >= 32.15 && e.z <= 32.85 && e.yd == .2f);
        assert(countOf(inv, "item:4:0") == 0 && inv.selectedDamage() == 1);
        assert(actions.miningInfoJson().find("\"inventoryFull\":false") != std::string::npos);
        // The player falls into the hole; after the 10-tick delay it is picked up.
        int frames = 0;
        mcplayer::Survival survival; survival.open(f.world);
        while (countOf(inv, "item:4:0") == 0 && frames < 60) {
            f.world.update(.05f, 0, 0, false, 0); scene.update(f.world, .05f);
            mcplayer::survivalFrame(survival, f.world, scene, inv, .05f); ++frames;
        }
        assert(countOf(inv, "item:4:0") == 1 && scene.items().empty() && frames >= 10);
    }
    {
        // A full inventory no longer matters: the block breaks and drops.
        Fixture f; mcscene::Scene scene; scene.open(f.world);
        mcinventory::Inventory inv; inv.open(f.world);
        assert(inv.selectHotbarItem(0, itemIndex(278), 1) && inv.select(0));
        for (int i = 1; i < 36; ++i) assert(inv.addItem(3, 0, 64));
        assert(inv.setGameMode(mcinventory::GameMode::Survival));
        auto actions = miningActions(scene);
        assert(mineDown(actions, f.world, inv) && scene.items().size() == 1);
    }
    {
        // DoTileDrops off -> no entity.
        Fixture f; mcscene::Scene scene; scene.open(f.world);
        assert(scene.setRule(mcworld::WorldRule::DoTileDrops, false));
        auto inv = survivalInventory(f.world);
        auto actions = miningActions(scene);
        assert(mineDown(actions, f.world, inv) && f.world.blockAt(32, 9, 32) == 0 && scene.items().empty());
    }
    {
        // Creative -> no entity.
        Fixture f; mcscene::Scene scene; scene.open(f.world);
        mcinventory::Inventory inv; inv.open(f.world);
        auto actions = miningActions(scene);
        assert(mineDown(actions, f.world, inv) && f.world.blockAt(32, 9, 32) == 0 && scene.items().empty());
    }
    ok("survival stone + pickaxe -> one cobblestone entity, inventory unchanged until pickup; full inventory still drops; DoTileDrops off / creative -> none");
}

void deathDrops() {
    // This port defaults KeepInventory to true (WorldRules.cpp), so turn it off.
    {
        Fixture f; mcscene::Scene scene; scene.open(f.world);
        assert(scene.setRule(mcworld::WorldRule::KeepInventory, false));
        auto inv = survivalInventory(f.world);
        assert(inv.addItem(297, 0, 3) && inv.addItem(4, 0, 64) && inv.addItem(310, 0, 1));
        assert(inv.select(2) && inv.equipSelectedArmor());
        mcplayer::Survival s; s.open(f.world);
        s.hurt(mcplayer::DamageSource::Generic, 25);
        f.world.update(.05f, 0, 0, false, 0);
        const auto r = mcplayer::survivalFrame(s, f.world, scene, inv, .05f);
        assert(r.died && !s.deathDropsPending() && r.pickedUp == 0);
        for (const auto& slot : inv.slots()) assert(!slot.count);
        for (const auto& slot : inv.armor()) assert(!slot.count);
        int bread = 0, cobble = 0, helmet = 0;
        for (const auto& e : scene.items()) {
            assert(e.pickupDelay == ItemEntities::PlayerDropDelay && std::abs(e.x - 32.5) < .01 && e.yd == .2f);
            if (e.itemId == 297) bread += e.count; if (e.itemId == 4) cobble += e.count; if (e.itemId == 310) helmet += e.count;
        }
        assert(bread == 3 && cobble == 64 && helmet == 1 && scene.deathDropsApplied());
        // Persisted: reopen shows the items in the world and an empty inventory.
        mcscene::Scene again; again.open(f.world); assert(again.items().size() == 3);
        mcinventory::Inventory inv2; inv2.open(f.world); for (const auto& slot : inv2.slots()) assert(!slot.count);
        // Dead players do not pick their items back up; after respawn they can.
        for (int i = 0; i < 45; ++i) {
            f.world.update(.05f, 0, 0, false, 0); scene.update(f.world, .05f);
            mcplayer::survivalFrame(s, f.world, scene, inv, .05f);
        }
        assert(countOf(inv, "item:297:0") == 0);
        s.respawn();
        // Player::drop(randomly) throws up to .5 blocks/tick: walk to each item.
        int taken = 0;
        for (int i = 0; i < 6 && !scene.items().empty(); ++i) {
            const auto target = scene.items().front();
            assert(f.world.teleport(float(target.x), 10, float(target.z)));
            f.world.update(.05f, 0, 0, false, 0); scene.update(f.world, .05f);
            taken += mcplayer::survivalFrame(s, f.world, scene, inv, .05f).pickedUp;
        }
        assert(taken == 68 && countOf(inv, "item:297:0") == 3 && scene.items().empty());
        // A second death clears the previous death's applied flag first.
        s.hurt(mcplayer::DamageSource::Generic, 25);
        f.world.update(.05f, 0, 0, false, 0);
        assert(mcplayer::survivalFrame(s, f.world, scene, inv, .05f).died);
        assert(scene.items().size() == 3 && scene.deathDropsApplied());
    }
    {
        // KeepInventory: nothing drops.
        Fixture f; mcscene::Scene scene; scene.open(f.world);
        assert(scene.setRule(mcworld::WorldRule::KeepInventory, true));
        auto inv = survivalInventory(f.world); assert(inv.addItem(297, 0, 3));
        mcplayer::Survival s; s.open(f.world); s.hurt(mcplayer::DamageSource::Generic, 25);
        f.world.update(.05f, 0, 0, false, 0);
        assert(mcplayer::survivalFrame(s, f.world, scene, inv, .05f).died);
        assert(scene.items().empty() && countOf(inv, "item:297:0") == 3);
    }
    {
        // Crash after the scene (items + applied flag) was saved but before the
        // inventory was cleared: reopening clears the inventory, no duplicate.
        Fixture f;
        {
            mcscene::Scene scene; scene.open(f.world);
            auto inv = survivalInventory(f.world); assert(inv.addItem(297, 0, 3)); inv.save(f.world);
            mcplayer::Survival s; s.open(f.world); s.setDeathDropsPending(true); s.save(f.world);
            scene.dropInventory(f.world, inv); scene.save(f.world);
        }
        mcscene::Scene scene; scene.open(f.world);
        mcinventory::Inventory inv; inv.open(f.world); assert(countOf(inv, "item:297:0") == 3);
        mcplayer::Survival s; s.open(f.world); assert(s.deathDropsPending() && scene.deathDropsApplied());
        mcplayer::survivalFrame(s, f.world, scene, inv, .05f);
        assert(countOf(inv, "item:297:0") == 0 && scene.items().size() == 1 && !s.deathDropsPending());
    }
    {
        // Crash after the pending flag but before the scene save: drop once on reopen.
        Fixture f;
        {
            mcscene::Scene scene; scene.open(f.world);
            assert(scene.setRule(mcworld::WorldRule::KeepInventory, false)); scene.save(f.world);
            auto inv = survivalInventory(f.world); assert(inv.addItem(297, 0, 3)); inv.save(f.world);
            mcplayer::Survival s; s.open(f.world); s.setDeathDropsPending(true); s.save(f.world);
        }
        mcscene::Scene scene; scene.open(f.world);
        mcinventory::Inventory inv; inv.open(f.world);
        mcplayer::Survival s; s.open(f.world);
        mcplayer::survivalFrame(s, f.world, scene, inv, .05f);
        assert(countOf(inv, "item:297:0") == 0 && scene.items().size() == 1 && scene.items()[0].count == 3);
        mcscene::Scene saved; saved.open(f.world); assert(saved.items().size() == 1);
    }
    ok("death drops main+armor as entities (throwTime 40), KeepInventory keeps, crash before/after scene save neither loses nor duplicates");
}

int changedPixels(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b) {
    int n = 0; for (size_t i = 0; i < a.size(); ++i) n += a[i] != b[i]; return n;
}

void rendering() {
    constexpr int W = 960, H = 266;
    // A stone wall at z = 40 (x 30..35, y 10..12) hides items behind it.
    std::vector<std::array<int, 4>> wall;
    for (int x = 28; x <= 37; ++x) for (int y = 10; y <= 13; ++y) wall.push_back({x, y, 40, blockId("stone")});
    Fixture f(wall);
    std::vector<uint32_t> frame(size_t(W) * H);
    renderWorldFrame(frame.data(), W, H, f.world, 0, -20, 3, 1, .25f);
    const auto base = frame;
    const auto draw = [&](const std::vector<ItemEntity>& items, float partial = 0) {
        frame = base; renderItemOverlay(frame.data(), W, H, f.world, 0, -20, items, 1, partial);
        return changedPixels(frame, base);
    };
    assert(draw({}) == 0);
    ItemEntity block = item(4, 1, 32.5, 10, 35.5);       // cobblestone: 3D block
    ItemEntity sprite = item(264, 1, 31.5, 10, 35.5);    // diamond: icon sprite
    ItemEntity flower = item(37, 1, 33.5, 10, 35.5);     // dandelion: terrain-icon sprite
    const int one = draw({block}), gem = draw({sprite}), plant = draw({flower});
    assert(one > 30 && gem > 20 && plant > 10);
    // Bigger stacks draw more copies (ItemRenderer count rule).
    ItemEntity stack = block; stack.count = 64;
    assert(draw({stack}) > one);
    // Bobbing/spinning: a later age changes the image.
    ItemEntity later = block; later.age = 17;
    draw({block}); const auto first = frame;
    assert(draw({later}) > 0 && frame != first);
    // Occluded by the wall, culled beyond 16 blocks, behind the camera.
    assert(draw({item(4, 1, 32.5, 10, 41.5)}) == 0);
    assert(draw({item(4, 1, 32.5, 10, 50.5)}) == 0);
    assert(draw({item(4, 1, 32.5, 10, 28.5)}) == 0);
    // Invalid inputs are no-ops.
    renderItemOverlay(nullptr, W, H, f.world, 0, 0, {block}, 1);
    renderItemOverlay(frame.data(), 0, H, f.world, 0, 0, {block}, 1);
    ok("renders blocks as 3D models and items/plants as sprites, stack copies, bob/spin, wall occlusion, 16-block cull");

    // Cost at 960x266 for 0, 20 and 128 items scattered 2..12 blocks ahead.
    const int ids[] = {4, 3, 264, 1, 37, 278, 17, 297};
    for (int n : {0, 20, 128}) {
        std::vector<ItemEntity> items;
        for (int i = 0; i < n; ++i) {
            ItemEntity e = item(ids[i % 8], 1 + (i * 7) % 64, 28.5 + (i % 9), 10, 34.5 + (i / 9) % 5);
            if (e.itemId == 278) e.count = 1;
            e.age = i * 13; e.bobOffs = float(i) * .37f;
            items.push_back(e);
        }
        const int frames = 50;
        const auto start = std::chrono::steady_clock::now();
        for (int k = 0; k < frames; ++k) {
            frame = base;
            for (auto& e : items) ++e.age;
            renderItemOverlay(frame.data(), W, H, f.world, 0, -20, items, 1, .5f);
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / frames;
        std::printf("PERF items=%d %.3f ms/frame at %dx%d\n", n, ms, W, H);
        if (n == 0) assert(ms < 1);
    }
    ok("render cost reported for 0/20/128 items");
}
} // namespace

int main() {
    physics(); validation(); mergeDespawnCap(); pickup(); persistence();
    miningDrops(); deathDrops(); rendering();
    std::printf("item-entities: %d groups passed\n", passed);
    return 0;
}
