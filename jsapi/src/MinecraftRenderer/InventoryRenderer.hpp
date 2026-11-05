#pragma once

#include <array>
#include <cstdint>
#include "Inventory.hpp"
#include "ContainerPanel.hpp"
#include <vector>

namespace mcinventory {
struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    bool contains(int px, int py) const {
        return px >= x && py >= y && int64_t(px) < int64_t(x) + w && int64_t(py) < int64_t(y) + h;
    }
};
struct InventoryLayout {
    float scale = 0;
    Rect hotbar, panel;
    std::array<Rect, 9> hotbarSlots {}; // 20x22 tap cells, ids 0..8.
    std::array<Rect, 9> hotbarIcons {}; // Source 16x16 icons.
    std::array<Rect, 9> hotbarSelections {}; // Source 24x22, (-1+20*i,-1).
    std::array<Rect, 36> tileSlots {}; // Full panel 16x16 icons, indexed by inventory id.
    std::array<Rect, 36> tileHitSlots {}; // Same centers, 18x18 tap cells.
    std::array<Rect, 4> armorSlots {}; // Display top..bottom (state indices 3..0).
    // Clip rectangle for the panel figure, in source panel units (34,13) 35x64.
    // Chosen to contain the reconstructed figure and to stay disjoint from every
    // slot rectangle above; renderPanelPlayer never writes outside it.
    Rect playerView {};
    // Returns inventory id, -1 outside. Armor/crafting are deliberately inactive.
    int hitTest(int x, int y, bool fullInventory) const;
};
// Platform adaptation of the source 1x GUI units. The source 184x168 minimum is
// enlarged by whole quarter steps when the viewport allows it, so 960x266 renders
// at 1.5x; viewports below the source minimum still shrink uniformly. The cap is
// 1.5x because that is the largest quarter step whose 168-unit chest panel fits
// the 266 px height (1.75x would need 294 px), and every source texel then maps
// to whole or half pixels. At 1.5x the hotbar spans x 343..616 (selection frames
// 341..618); the right-side world buttons start at x=626 to stay clear of it.
// Texture aspect is never stretched: both axes
// use one scale. No invented safe-zone, console split-screen or user GUI-scale
// setting. Integer edge rounding is shared by renderer and hit testing. Panel
// centered, hotbar bottom-center, no controls.
inline constexpr float kMaxGuiScale = 1.5f;
InventoryLayout inventoryLayout(int w, int h);
// Panel figure drawn inside layout.playerView only, for the full inventory.
//
// HONEST SCOPE: the source InventoryScreen::renderBg body that draws this figure
// is wrapped in `#if 0 // 4J Unused` (InventoryScreen.cpp:40-41,83), so THIS
// VERSION NEVER DRAWS IT AT RUNTIME. What follows is a reconstruction of that
// disabled block's documented transform parameters, not a port of observed
// behavior of this build:
//   anchor (xo+51, yo+75) panel-relative, ss=30 with glScalef(-ss,ss,ss) (X
//   mirrored), glRotatef(180,0,0,1), playerRotY=180, heightOffset=1.62.
// Device adaptation: the source head tracking (xMouse/yMouse via atan(xd/40))
// cannot apply because this platform has no pointer, so the pose is FIXED at the
// source's zero-offset orientation instead of animated. Base Steve skin only; the
// armor model layer is not ported, so equipped armor is deliberately not shown.
// Geometry reuses the source HumanoidModel boxes (HumanoidModel.cpp:72-101).
void renderPanelPlayer(uint32_t* argb, int w, int h, const InventoryLayout& layout);
// ARGB32 A<<24|R<<16|G<<8|B overlay; no clear/no UI dependencies. Full panel
// uses original inventory.png, grid and inactive crafting/armor silhouettes.
// Canonical non-block items use source atlas sprites; block items use a
// representative terrain-state thumbnail, not the complete original ItemRenderer.
void renderInventoryOverlay(uint32_t* argb, int w, int h,
                            const Inventory& inventory, bool fullInventory);
// Container screens, source slot coordinates (1x GUI units, slot icon origin):
//   Inventory  InventoryMenu.cpp:35,41  grid 88+x*18,26+y*18 (2x2), result 144,36
//   Workbench  CraftingMenu.cpp:29,35   grid 30+x*18,17+y*18 (3x3), result 124,35
//   Furnace    FurnaceMenu.cpp:19-21    input 56,17  fuel 56,53  output 116,35
//   Chest      ContainerMenu.cpp:23     27 slots 8+x*18,18+y*18; player rows
//              shifted by yo=(3-4)*18 (ContainerMenu.cpp:17,31,36); panel height
//              222-6*18+3*18=168 (ContainerScreen.cpp:12-16).
// Player rows otherwise follow InventoryMenu (84+y*18, hotbar 142). Same scale
// and centering rule as inventoryLayout; hit cells are the 18x18 slot frames.
struct ContainerLayout {
    float scale = 0;
    PanelKind kind = PanelKind::None;
    Rect panel;
    std::array<Rect, 36> inventoryIcons {}, inventoryHits {}; // by inventory id
    std::vector<Rect> slotIcons, slotHits;                     // by container slot
    bool hasResult = false;
    Rect resultIcon {}, resultHit {};
    Rect flame {}, arrow {}; // Furnace progress boxes (full extent), else empty.
    // Hit-test ids: 0..35 inventory, kSlotBase+i container slot, kResult, -1 none.
    static constexpr int kSlotBase = 100, kResult = 200;
    int hitTest(int x, int y) const;
};
ContainerLayout containerLayout(int w, int h, PanelKind kind);
// Draws one container screen (background, player rows, panel slots, result
// preview, furnace flame/arrow). Inventory kind is the full inventory overlay
// plus its 2x2 grid and result. No-op for PanelKind::None.
void renderContainerOverlay(uint32_t* argb, int w, int h, const Inventory& inventory, const PanelView& view);
} // namespace mcinventory
