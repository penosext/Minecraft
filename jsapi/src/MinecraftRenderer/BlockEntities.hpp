#pragma once

#include "Inventory.hpp"
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace mcworld { class World; }

namespace mccontainers {
using mcworld::World;
using mcinventory::Inventory;

// Main-thread only. Single chests (54), persistent 3x3 workbenches (58),
// furnaces (61/62). No double chests, hoppers, automation or offline ticking.
class Containers {
public:
    // Transactional read; missing containers.bin stays absent until a mutation
    // is explicitly saved. Errors throw; invalid files never replace live state.
    void open(const World&);
    void save(const World&);
    bool interact(World&, int x, int y, int z);
    void close(); // Closes only the panel; never discards slots or stops furnaces.
    bool opened() const; // Whether a panel is open, not whether a world is bound.
    std::string infoJson(const Inventory&) const;
    // Direction is inventory -> container iff toContainer. Output is extract-only.
    // Moves/merges up to count; full-stack identity swaps preserve both stacks.
    bool transfer(World&, Inventory&, int inventorySlot, int containerSlot, int count, bool toContainer);
    bool craft(World&, Inventory&); // One matched recipe; atomic inventory capacity check.
    // 20 TPS; only loaded cells. Bounded round-robin (64 records, 200 ticks per
    // record/call); excess elapsed time is dropped, not replayed after loading.
    void update(World&, float dt);
    // Main MUST check canBreak before mining, then call removed after success.
    // Occupied grids/chests/furnaces and burning furnaces cannot be destroyed.
    bool canBreak(int x, int y, int z) const;
    void removed(int x, int y, int z); // Refuses nonempty/burning records.
    // Atomically stage main+armor into a persistent, dimension-bound death pile;
    // legacy protected data is untouched. Collection leaves any unaccepted stack.
    void dropOnDeath(const World&, Inventory&);
    void collectNearby(const World&, Inventory&); // Caller enforces alive/main thread.

private:
    using Position = std::array<int, 3>;
    struct Entity {
        int tile = 0; // 54, 58 or normalized furnace 61.
        std::vector<mcinventory::Slot> slots;
        int burn = 0, burnTotal = 0, cook = 0;
        double fraction = 0;
    };
    void requireWorld(const World&) const;
    Entity* active(World&);
    bool bound_ = false, panel_ = false, dirty_ = false;
    Position selected_ {}, cursor_ {};
    bool cursorValid_ = false;
    std::map<Position, Entity> entities_;
    std::string root_, path_;
    uint64_t rootDevice_ = 0, rootInode_ = 0, worldDevice_ = 0, worldInode_ = 0;
    uint32_t seed_ = 0, sourceHash_ = 0;
    int width_ = 0, height_ = 0, depth_ = 0, dimension_ = 0;
    bool original_ = false;
    std::vector<unsigned char> source_, persisted_;
};
} // namespace mccontainers
