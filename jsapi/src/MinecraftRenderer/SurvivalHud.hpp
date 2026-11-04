#pragma once

#include <cstdint>

#include "InventoryRenderer.hpp"

namespace mcplayer { class Survival; }

namespace mcplayer {
// Positions are legacy Gui.cpp units relative to the hotbar origin, scaled by
// the same quarter-step GUI scale and rounding as the hotbar
// (mcinventory::inventoryLayout). xLeft = hotbar x, xRight = hotbar x + 182,
// yLine1 = hotbar y - 18, yLine2 = yLine1 - 10 (Gui.cpp:429-441).
struct HudLayout {
    mcinventory::Rect hearts[10], armor[10], food[10], air[10];
};
HudLayout hudLayout(int w, int h);
// Draws hearts, armor, hunger and (under water) air like Gui.cpp:452-623 with
// IUIScene_HUD.cpp values (ceil health, invulnerability blink). Nothing in
// creative (GameMode::canHurtPlayer false) or while a full panel is open. A
// dead player gets the DeathScreen.cpp:45 gradient over the frame instead.
void renderSurvivalHud(uint32_t* argb, int w, int h, const Survival& survival,
                       bool creative, bool panelOpen, bool underWater);
} // namespace mcplayer
