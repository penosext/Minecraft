// Survival regressions: FoodData, regen/starvation, fall, drowning, armor,
// eating, death drops and survival.bin v1 -> v2 compatibility.
#define main worldRegressionMain
#include "world-test.cpp"
#undef main
#include "Inventory.hpp"
#include "ItemCatalog.hpp"
#include "PlayerSurvival.hpp"
#include "Scene.hpp"
#include "SurvivalFrame.hpp"

using mcplayer::DamageSource;
using mcplayer::Survival;

namespace {
int passed = 0;
void ok(const char* name) { ++passed; std::printf("PASS %s\n", name); }
bool approx(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

struct Fixture {
    Temp root;
    World world;
    std::string path;
    Bytes flat;
    Fixture() {
        World original; original.create(902, root.path, false); path = original.path();
        flat = read(path + "/chunks.bin");
        std::fill(flat.begin() + 40, flat.end(), 0);
        for (int z = 0; z < mcworld::Depth; ++z) for (int x = 0; x < mcworld::Width; ++x)
            block(flat, x, 9, z, blockId("stone"));
        install(flat);
    }
    void install(const Bytes& chunks, float x = 32.5f, float y = 10, float z = 32.5f) {
        fs::remove(path + "/edits.bin"); synchronize(path, chunks); player(path, x, y, z);
        world = World(); world.open(path, root.path);
    }
};
// Runs whole 20 Hz ticks through update() (two per .1s call).
void ticks(Survival& s, World& w, int n, bool regen = true) {
    for (int i = 0; i < n; ++i) { w.update(.05f, 0, 0, false, 0); s.update(w, .05f, false, regen); }
}
int foodIndex(int id) { return sourceitems::itemIndexForIdAux(id, 0); }
} // namespace

void foodData() {
    Fixture f; Survival s; s.open(f.world);
    assert(s.food() == 20 && approx(s.saturation(), 5) && s.health() == 20);
    // FoodData::eat: food capped at 20, saturation += n * mod * 2, capped at food.
    s.exhaust(4.0f); ticks(s, f.world, 1);          // exactly 4 is NOT > 4
    assert(approx(s.saturation(), 5));
    s.exhaust(.5f); ticks(s, f.world, 1);           // > 4: saturation first
    assert(approx(s.saturation(), 4) && s.food() == 20);
    for (int i = 0; i < 4; ++i) { s.exhaust(4.5f); ticks(s, f.world, 1); }
    assert(approx(s.saturation(), 0) && s.food() == 20);
    s.exhaust(4.5f); ticks(s, f.world, 1);
    assert(s.food() == 19);
    // Bread 5 / .6: food 20 (capped), saturation min(0 + 6, 20) = 6.
    assert(s.eat(5, .6f) && s.food() == 20 && approx(s.saturation(), 6));
    assert(!s.eat(5, .6f)); // needsFood is false at 20
    // Exhaustion is capped at MAX_SATURATION * 2 = 40.
    s.exhaust(100); assert(approx(s.exhaustion(), 40));
    ok("FoodData eat/exhaustion/saturation (FoodData.cpp:21-51,132-135)");
}

void regenAndStarve() {
    Fixture f; Survival s; s.open(f.world);
    s.hurt(DamageSource::Generic, 5); assert(approx(s.health(), 15));
    ticks(s, f.world, 79); assert(approx(s.health(), 15));
    ticks(s, f.world, 1);  assert(approx(s.health(), 16));       // HEALTH_TICK_COUNT 80
    assert(s.exhaustion() >= 3.0f);                            // EXHAUSTION_HEAL 3
    // Natural regeneration off: no heal.
    Survival off; off.open(f.world); off.hurt(DamageSource::Generic, 5);
    ticks(off, f.world, 200, false); assert(approx(off.health(), 15));
    // Starvation by difficulty: drain food to 0, then wait.
    for (int difficulty : {1, 2, 3}) {
        Survival st; st.open(f.world); st.setDifficulty(difficulty);
        while (st.food() > 0) { st.exhaust(4.5f); ticks(st, f.world, 1); }
        st.hurt(DamageSource::Generic, 9); // health 11 -> starve
        ticks(st, f.world, 2000);
        const float floor = difficulty == 1 ? 10.0f : difficulty == 2 ? 1.0f : 0.0f;
        assert(approx(st.health(), floor));
        assert(st.dead() == (difficulty == 3));
    }
    ok("natural regen 80 ticks / starvation floors easy 10, normal 1, hard death (FoodData.cpp:68-89)");
}

void fallDamage() {
    Fixture f; Survival s; s.open(f.world);
    f.world.update(.05f, 0, 0, false, 0); s.update(f.world, .05f, false, true);
    assert(f.world.teleport(32.5f, 20.0f, 32.5f));          // 10 blocks above floor y=10
    for (int i = 0; i < 200 && !f.world.grounded(); ++i) { f.world.update(.05f, 0, 0, false, 0); s.update(f.world, .05f, false, true); }
    ticks(s, f.world, 2);
    // ceil(10 - 3) = 7, bypasses armor.
    assert(approx(s.health(), 13));
    Survival c; c.open(f.world);
    f.world.update(.05f, 0, 0, false, 0); c.update(f.world, .05f, true, true);
    f.world.teleport(32.5f, 20.0f, 32.5f);
    for (int i = 0; i < 200 && !f.world.grounded(); ++i) { f.world.update(.05f, 0, 0, false, 0); c.update(f.world, .05f, true, true); }
    assert(c.health() == 20);
    ok("fall damage ceil(d - 3), none in creative (LivingEntity.cpp:1023-1030)");
}

void drowning() {
    Fixture f;
    auto chunks = f.flat;
    for (int y = 10; y < 14; ++y) for (int z = 30; z < 35; ++z) for (int x = 30; x < 35; ++x)
        block(chunks, x, y, z, blockId("calmWater"));
    f.install(chunks);
    Survival s; s.open(f.world);
    f.world.update(.05f, 0, 0, false, 0);
    assert(f.world.underWater());
    ticks(s, f.world, 300); assert(s.air() == 0 && s.health() == 20);
    ticks(s, f.world, 20); assert(approx(s.health(), 18)); // air -20 -> 2 drown damage
    ok("air supply 300, drown 2 every 20 ticks (LivingEntity::baseTick)");
}

void armor() {
    Fixture f; Survival s; s.open(f.world);
    s.setArmorValue(20); // full diamond
    // Mob attack, normal: 3 * (25 - 20) / 25 = .6; wear gets the raw 3.
    assert(s.hurt(DamageSource::MobAttack, 3) && approx(s.health(), 19.4f));
    auto wear = s.takeArmorWear(); assert(wear.size() == 1 && approx(wear[0], 3));
    // Fall bypasses armor and produces no wear.
    Survival b; b.open(f.world); b.setArmorValue(20);
    b.hurt(DamageSource::Fall, 4); assert(approx(b.health(), 16) && b.takeArmorWear().empty());
    // Difficulty: easy dmg/2+1, hard *1.5, peaceful 0.
    Survival e; e.open(f.world); e.setDifficulty(1); e.hurt(DamageSource::MobAttack, 3); assert(approx(e.health(), 17.5f));
    Survival h; h.open(f.world); h.setDifficulty(3); h.hurt(DamageSource::MobAttack, 3); assert(approx(h.health(), 15.5f));
    Survival p; p.open(f.world); p.setDifficulty(0); assert(!p.hurt(DamageSource::MobAttack, 3) && p.health() == 20);
    // Invulnerability: within 10 ticks only the excess applies.
    Survival i; i.open(f.world); i.hurt(DamageSource::Generic, 3); assert(!i.hurt(DamageSource::Generic, 2));
    assert(i.hurt(DamageSource::Generic, 5) && approx(i.health(), 15));
    // Inventory: equip swaps into armor_[3 - slot], value sums defense, wear breaks above max.
    mcinventory::Inventory inv; inv.open(f.world); assert(inv.setGameMode(mcinventory::GameMode::Survival));
    assert(inv.addItem(310, 0, 1) && inv.select(0)); // diamond helmet, defense 3
    assert(inv.selectedItemId() == 310 && inv.equipSelectedArmor());
    assert(inv.emptyHand() && inv.armor()[3].key == "item:310:0" && inv.armorValue() == 3);
    inv.hurtArmor(3);                                // max(1, 3/4) -> 1
    assert(inv.armor()[3].damage == 1);
    inv.hurtArmor(4000); assert(inv.armor()[3].count == 0 && inv.armorValue() == 0);
    assert(inv.addItem(297, 0, 1) && !inv.equipSelectedArmor()); // bread is not armor
    ok("armor absorb (25 - armor)/25, bypass sources, difficulty scaling, invuln, equip/wear (LivingEntity.cpp:1085-1094)");
}

void eating() {
    Fixture f; mcscene::Scene scene; scene.open(f.world);
    mcinventory::Inventory inv; inv.open(f.world); inv.setGameMode(mcinventory::GameMode::Survival);
    Survival s; s.open(f.world);
    const auto* bread = sourceitems::findFood(297);
    assert(bread && bread->nutrition == 5 && approx(bread->saturationModifier, .6f) && bread->eatTicks == 32);
    assert(foodIndex(297) >= 0 && sourceitems::findFood(1) == nullptr && sourceitems::findFood(282) == nullptr);
    assert(inv.addItem(297, 0, 2) && inv.select(0));
    // Full food: canEat is false.
    assert(!mcplayer::useHeldItem(s, inv, true, true));
    while (s.food() > 14) { s.exhaust(4.5f); ticks(s, f.world, 1); }
    const int before = s.food();
    assert(mcplayer::useHeldItem(s, inv, true, true) && s.eating() && approx(s.movementScale(), .2f));
    for (int i = 0; i < 31; ++i) { f.world.update(.05f, 0, 0, false, 0); mcplayer::survivalFrame(s, f.world, scene, inv, .05f); }
    assert(s.eating() && inv.selectedCount() == 2 && s.food() <= before);
    const int mid = s.food();
    f.world.update(.05f, 0, 0, false, 0);
    const auto r = mcplayer::survivalFrame(s, f.world, scene, inv, .05f);
    assert(r.ate && inv.selectedCount() == 1 && s.food() == mid + 5 && !s.eating());
    // Release before completion cancels without consuming.
    assert(mcplayer::useHeldItem(s, inv, true, true));
    for (int i = 0; i < 10; ++i) { f.world.update(.05f, 0, 0, false, 0); mcplayer::survivalFrame(s, f.world, scene, inv, .05f); }
    mcplayer::useHeldItem(s, inv, false, false);
    for (int i = 0; i < 40; ++i) { f.world.update(.05f, 0, 0, false, 0); mcplayer::survivalFrame(s, f.world, scene, inv, .05f); }
    assert(inv.selectedCount() == 1);
    // Creative never eats.
    inv.setGameMode(mcinventory::GameMode::Creative);
    f.world.update(.05f, 0, 0, false, 0); mcplayer::survivalFrame(s, f.world, scene, inv, .05f);
    assert(!mcplayer::useHeldItem(s, inv, true, true));
    ok("hold-to-eat 32 ticks consumes one then FoodData::eat; release cancels; creative refuses (FoodItem.cpp:36-71)");
}

void deathAndRespawn() {
    for (bool keep : {false, true}) {
        Fixture f; mcscene::Scene scene; scene.open(f.world);
        assert(scene.setRule(mcworld::WorldRule::KeepInventory, keep));
        mcinventory::Inventory inv; inv.open(f.world); inv.setGameMode(mcinventory::GameMode::Survival);
        assert(inv.addItem(297, 0, 3));
        Survival s; s.open(f.world);
        s.hurt(DamageSource::Generic, 25);
        assert(s.dead());
        f.world.update(.05f, 0, 0, false, 0);
        const auto r = mcplayer::survivalFrame(s, f.world, scene, inv, .05f);
        assert(r.died && !s.deathDropsPending());
        assert(inv.slots()[0].count == (keep ? 3 : 0));
        // Persisted: reopening sees a dead player and the cleared inventory.
        Survival again; again.open(f.world); assert(again.dead() && again.persistedVersion() == 2);
        mcinventory::Inventory inv2; inv2.open(f.world); assert(inv2.slots()[0].count == (keep ? 3 : 0));
        s.respawn(); assert(!s.dead() && s.health() == 20 && s.food() == 20 && approx(s.saturation(), 5));
    }
    ok("death clears inventory unless KeepInventory, persisted; respawn resets stats (Player.cpp:1090-1105)");
}

void saveCompat() {
    Fixture f;
    {
        Survival s; s.open(f.world); assert(s.persistedVersion() == 0);
        s.hurt(DamageSource::Generic, 3); s.save(f.world);
    }
    Bytes v2 = read(f.path + "/survival.bin");
    assert(v2.size() == 116 && v2[8] == 2);
    // Hand-build the legacy v1 file: version 1, no flags word, re-checksummed.
    Bytes v1(v2.begin(), v2.end() - 8);
    u32(v1, 8, 1); v1.resize(v1.size() + 4); u32(v1, v1.size() - 4, hash(v1, v1.size() - 4));
    assert(v1.size() == 112);
    write(f.path + "/survival.bin", v1);
    Survival old; old.open(f.world);
    assert(old.persistedVersion() == 1 && approx(old.health(), 17) && !old.deathDropsPending());
    old.save(f.world);
    assert(read(f.path + "/survival.bin").size() == 116);
    Survival upgraded; upgraded.open(f.world); assert(upgraded.persistedVersion() == 2 && approx(upgraded.health(), 17));
    // Mismatched version/size and unknown flags are rejected.
    Bytes bad = v1; u32(bad, 8, 2); u32(bad, bad.size() - 4, hash(bad, bad.size() - 4));
    write(f.path + "/survival.bin", bad);
    rejects([&] { Survival x; x.open(f.world); });
    Bytes flags = v2; u32(flags, flags.size() - 8, 2); u32(flags, flags.size() - 4, hash(flags, flags.size() - 4));
    write(f.path + "/survival.bin", flags);
    rejects([&] { Survival x; x.open(f.world); });
    // A saved pending death is re-applied on the next frame.
    Bytes pending = v2; u32(pending, pending.size() - 8, 1); u32(pending, pending.size() - 4, hash(pending, pending.size() - 4));
    write(f.path + "/survival.bin", pending);
    mcscene::Scene scene; scene.open(f.world); assert(scene.setRule(mcworld::WorldRule::KeepInventory, false));
    mcinventory::Inventory inv; inv.open(f.world); inv.setGameMode(mcinventory::GameMode::Survival); assert(inv.addItem(297, 0, 2));
    Survival p; p.open(f.world); assert(p.deathDropsPending());
    mcplayer::survivalFrame(p, f.world, scene, inv, .05f);
    assert(!p.deathDropsPending() && inv.slots()[0].count == 0);
    ok("survival.bin v1 (112 B) opens, save upgrades to v2 (116 B, flags), bad version/flags rejected, pending drops re-applied");
}

int main() {
    foodData(); regenAndStarve(); fallDamage(); drowning(); armor(); eating(); deathAndRespawn(); saveCompat();
    std::printf("survival: %d groups passed\n", passed);
    return 0;
}
