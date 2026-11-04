#pragma once

#include "Inventory.hpp"
#include "ContainerPanel.hpp"
#include "Crafting.hpp"
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace mcworld { class World; }

namespace mccontainers {
using mcworld::World;
using mcinventory::Inventory;

// Main-thread only. Screens: the player 2x2 crafting grid (InventoryMenu),
// workbenches (58, CraftingMenu), single chests (54, ContainerMenu 3 rows) and
// furnaces (61, FurnaceTileEntity). No double chests, hoppers, lit-furnace
// block swap (62 is absent from the render catalog) or offline ticking.
class Containers {
public:
    enum class Screen { None = 0, Inventory, Workbench, Chest, Furnace };
    enum class CraftResult { Ok = 0, NoPanel, NoRecipe, InventoryFull };

    // Transactional read; missing containers.bin stays absent until a mutation
    // is explicitly saved. Errors throw; invalid files never replace live state.
    // Reads v1 and v2; always writes v2 (v1 + player 2x2 grid trailer).
    void open(const World&);
    void save(const World&);
    // Tile::use for 54/58/61/62 (ChestTile.cpp:265, WorkbenchTile.cpp:35,
    // FurnaceTile.cpp:111). Opens the panel; refuses while another is open.
    bool interact(World&, int x, int y, int z);
    // Player inventory screen (InventoryMenu): opens the persistent 2x2 grid.
    bool openInventory(const World&);
    // Closes the panel. Crafting grids (2x2 and workbench) return their items
    // to the inventory like CraftingMenu::removed / InventoryMenu::removed;
    // whatever does not fit stays in the grid record (persisted) instead of
    // the source's player->drop, because this port has no item entities.
    // Chest/furnace slots are never touched. Returns items left behind.
    int close(const World&, Inventory&);
    bool opened() const { return screen_ != Screen::None; }
    Screen screen() const { return screen_; }
    std::string infoJson(const Inventory&) const;
    mcinventory::PanelView view() const;
    // Direction is inventory -> container iff toContainer. Furnace output is
    // extract-only (FurnaceResultSlot::mayPlace false). Moves/merges up to
    // count; full-stack identity swaps preserve both stacks.
    bool transfer(World&, Inventory&, int inventorySlot, int containerSlot, int count, bool toContainer);
    // Shift-click analogue (*Menu::quickMoveStack): moves as much of one
    // container slot as fits into the inventory. Returns false if nothing moved.
    bool quickMove(World&, Inventory&, int containerSlot);
    // ResultSlot preview of the active crafting grid; matched=false otherwise.
    crafting::Match preview() const;
    // One ResultSlot::onTake: consumes one item from every occupied grid slot
    // (ResultSlot.cpp:60-66) and adds the product to the inventory atomically.
    CraftResult craft(World&, Inventory&);
    // 20 TPS FurnaceTileEntity::tick on every furnace whose block is resident,
    // whether or not its panel is open. dt is clamped to 0.1 s per call.
    void update(World&, float dt);
    // Main MUST check canBreak before mining, then call removed after success.
    // Non-empty chests/furnaces/grids and burning furnaces refuse the break
    // (source drops contents as item entities; this port has none).
    bool canBreak(int x, int y, int z) const;
    void removed(int x, int y, int z);
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
    std::vector<mcinventory::Slot>* activeSlots();
    const std::vector<mcinventory::Slot>* activeSlots() const;
    const Entity* activeEntity() const;
    bool bound_ = false, dirty_ = false;
    Screen screen_ = Screen::None;
    Position selected_ {};
    std::map<Position, Entity> entities_;
    std::vector<mcinventory::Slot> playerGrid_ = std::vector<mcinventory::Slot>(4);
    std::string root_, path_;
    uint64_t rootDevice_ = 0, rootInode_ = 0, worldDevice_ = 0, worldInode_ = 0;
    uint32_t seed_ = 0, sourceHash_ = 0;
    int width_ = 0, height_ = 0, depth_ = 0, dimension_ = 0;
    bool original_ = false;
    std::vector<unsigned char> source_, persisted_;
};
} // namespace mccontainers
