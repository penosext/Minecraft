#pragma once
// Plain data shared by the container model (BlockEntities) and the native GUI
// renderer (InventoryRenderer), so the renderer never links world/save code.
#include "Inventory.hpp"

#include <vector>

namespace mcinventory {
enum class PanelKind { None = 0, Inventory, Workbench, Chest, Furnace };

struct PanelView {
    PanelKind kind = PanelKind::None;
    // Inventory: 2x2 grid (row major). Workbench: 3x3 grid. Chest: 27.
    // Furnace: 0 input, 1 fuel, 2 output (FurnaceTileEntity SLOT_* order).
    std::vector<Slot> slots;
    Slot result;          // Crafting preview only (Inventory/Workbench); never stored.
    int litProgress = 0;  // Furnace flame, FurnaceTileEntity::getLitProgress(12): 0..12.
    int burnProgress = 0; // Furnace arrow, getBurnProgress(24): 0..24.
    bool lit = false;     // litTime > 0, i.e. FurnaceScreen draws the flame.
};
} // namespace mcinventory
