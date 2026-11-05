#pragma once

#include "SceneTypes.hpp"

#include <cstdint>
#include <vector>

namespace mcworld { class World; }
namespace mcinventory { class Inventory; struct Slot; }

namespace mcscene {
// Dropped items: a bounded, main-thread store of ItemEntity physics, merging,
// despawn and pickup (Minecraft.World ItemEntity.cpp, Entity::move/checkInTile,
// Tile::popResource, Player::drop, Player::aiStep pickup). Owned by Scene, which
// ticks it at the scene's 20 Hz and persists it in items.bin.
//
// Platform choices, not source behaviour:
//  - Capacity 128 (the source ServerLevel caps at MAX_ITEM_ENTITIES = 200,
//    ServerLevel.h:139, and also removes the OLDEST on overflow,
//    ServerLevel.cpp:1307-1315). The slower pen gets a smaller cap.
//  - Items whose box touches an unloaded/streaming column are frozen (no
//    gravity, ageing or merging) instead of falling through unknown terrain.
//  - Liquids do not push items (no Level::checkAndHandleWater flow vectors).
//  - Randomness uses the original Random seeded per event, not Math::random.
class ItemEntities {
public:
    static constexpr int Capacity = 128;
    static constexpr int Lifetime = 5 * 60 * 20;   // ItemEntity.h:16 LIFETIME
    static constexpr int BlockDropDelay = 10;      // Tile.cpp:938 throwTime = 10
    static constexpr int PlayerDropDelay = 40;     // Player.cpp:1174 throwTime = 20 * 2
    static constexpr float Size = .25f;            // ItemEntity.cpp:29 setSize(.25, .25)

    void reset(uint32_t seed) { items_.clear(); seed_ = seed; serial_ = 0; }
    const std::vector<ItemEntity>& items() const { return items_; }
    size_t size() const { return items_.size(); }
    uint64_t serial() const { return serial_; }

    // Adds a validated entity; the oldest is removed first when full. Returns
    // false (nothing changed) for an unknown item identity or invalid count.
    bool add(ItemEntity item);
    // Tile::popResource (Tile.cpp:929-940): random offset .15..85 in the cell,
    // ItemEntity ctor velocity (ItemEntity.cpp:41-43), throwTime 10.
    bool popResource(int x, int y, int z, int itemId, int aux, int count, int damage = 0);
    // ChestTile::onRemove / FurnaceTile::onRemove (ChestTile.cpp:214-256,
    // FurnaceTile.cpp:162-210): per slot one random .1..9 offset, split into
    // random 10..30 stacks, gaussian * .05 velocity (+.2 up), throwTime 0.
    void dropContents(int x, int y, int z, const std::vector<mcinventory::Slot>& slots);
    // Player::drop(item, randomly = true) (Player.cpp:1168-1187) at the player's
    // feet + 1.62 - .3 - .12 (Player head height), throwTime 40.
    bool dropFromPlayer(float feetX, float feetY, float feetZ, const mcinventory::Slot& slot);

    // One 20 Hz ItemEntity::tick for every resident item.
    void tick(const mcworld::World& world);
    // Player::aiStep touch (Player.cpp:1039-1064) + ItemEntity::playerTouch
    // (ItemEntity.cpp:221-264): box = player bb grown by (1, .5, 1). Partial
    // pickup keeps the remainder; creative instabuild destroys what does not
    // fit (Inventory.cpp:379-383). Returns the number of items taken.
    int pickup(const mcworld::World& world, mcinventory::Inventory& inventory, bool alive);

    // Persistence helpers (Scene owns the file). replace() validates first and
    // leaves *this unchanged when any record is invalid.
    bool replace(std::vector<ItemEntity> items, uint64_t serial);
    static bool valid(const ItemEntity& item);

private:
    int64_t nextSeed();
    void merge(size_t index);
    std::vector<ItemEntity> items_;
    uint32_t seed_ = 0;
    uint64_t serial_ = 0;
};

// Splits "item:<id>:<aux>"; false for any other key.
bool parseItemKey(const char* key, int& itemId, int& aux);
} // namespace mcscene
