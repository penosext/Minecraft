// Survival HUD: layout vs hotbar, hearts/food/armor/air drawn from the pinned
// atlas, hidden in creative/panel, death gradient. No World needed.
#include "SurvivalHud.hpp"
#include "HudIcons.hpp"
#include "PlayerSurvival.hpp"

#include <cassert>
#include <cstdio>
#include <vector>

using mcplayer::Survival;
namespace {
constexpr int W = 960, H = 266;
constexpr uint32_t Bg = 0xff102030u;
std::vector<uint32_t> frame(const Survival& s, bool creative, bool panel, bool water) {
    std::vector<uint32_t> px(size_t(W) * H + 2, Bg);
    px.front() = px.back() = 0xc0ffee00u;
    mcplayer::renderSurvivalHud(px.data() + 1, W, H, s, creative, panel, water);
    assert(px.front() == 0xc0ffee00u && px.back() == 0xc0ffee00u);
    return std::vector<uint32_t>(px.begin() + 1, px.end() - 1);
}
int changed(const std::vector<uint32_t>& f, mcinventory::Rect r) {
    int n = 0;
    for (int y = r.y; y < r.y + r.h; ++y) for (int x = r.x; x < r.x + r.w; ++x) n += f[size_t(y) * W + x] != Bg;
    return n;
}
uint32_t atlas(hudicons::Cell c, int x, int y) {
    return hudicons::kPixels[(c.v - hudicons::kAtlasY + y) * hudicons::kWidth + (c.u - hudicons::kAtlasX + x)];
}
bool overlaps(mcinventory::Rect a, mcinventory::Rect b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}
} // namespace

int main() {
    const auto inv = mcinventory::inventoryLayout(W, H);
    const auto hud = mcplayer::hudLayout(W, H);
    // Scale 1.25: yLine1 = hotbar.y + lround(-22.5) = 215, shared rounding with the hotbar.
    assert(inv.scale == 1.25f && inv.hotbar.y == 238 && inv.hotbar.x == 366);
    assert(hud.hearts[0].x == 366 && hud.hearts[0].y == 215 && hud.hearts[0].w == 11 && hud.hearts[0].h == 12);
    assert(hud.food[0].x + hud.food[0].w == 366 + 228 && hud.armor[0].y < hud.hearts[0].y);
    for (int i = 0; i < 10; ++i) {
        for (auto r : {hud.hearts[i], hud.armor[i], hud.food[i], hud.air[i]}) {
            assert(r.y >= 0 && r.y + r.h <= inv.hotbar.y && r.x >= 0 && r.x + r.w <= W);
            assert(!overlaps(r, inv.hotbar));
        }
        for (int j = 0; j < 10; ++j) assert(!overlaps(hud.hearts[i], hud.food[j]));
    }
    std::puts("PASS layout: Gui.cpp yLine1/yLine2/xLeft/xRight at the hotbar's 1.25x scale, no hotbar overlap");

    // Atlas really is the runtime 1_2_2 sheet: food cells are not empty.
    int foodAlpha = 0;
    for (int y = 0; y < 9; ++y) for (int x = 0; x < 9; ++x) foodAlpha += (atlas(hudicons::kFoodFull, x, y) >> 24) != 0;
    assert(foodAlpha == 20); // Exact opaque texel count of the 1_2_2 full shank.

    Survival s; // Defaults: 20 health, 20 food, armor 0.
    auto full = frame(s, false, false, false);
    for (int i = 0; i < 10; ++i) { assert(changed(full, hud.hearts[i]) > 20); assert(changed(full, hud.food[i]) > 20); }
    assert(changed(full, hud.armor[0]) == 0 && changed(full, hud.air[0]) == 0);
    // The full heart's center texel is the source red.
    const auto heart = hud.hearts[0];
    assert(full[size_t(heart.y + heart.h / 2) * W + heart.x + heart.w / 2] == atlas(hudicons::kHeartFull, 4, 4));
    std::puts("PASS hearts and hunger drawn from source cells; no armor at 0, no air above water");

    // Damage 5 -> 15: heart 7 is half (i*2+1 == 15), hearts 8/9 empty containers only.
    s.hurt(mcplayer::DamageSource::Generic, 5);
    s.setArmorValue(7);
    auto hurt = frame(s, false, false, true);
    assert(changed(hurt, hud.armor[0]) > 10 && changed(hurt, hud.armor[9]) > 10); // empty armor cells still drawn
    assert(changed(hurt, hud.air[0]) > 10 && changed(hurt, hud.air[9]) > 10);     // full air = 10 bubbles
    std::puts("PASS armor bar when armor > 0, air bubbles under water (IUIScene_HUD.cpp:212-250)");

    assert(frame(s, true, false, false) == std::vector<uint32_t>(size_t(W) * H, Bg));
    assert(frame(s, false, true, false) == std::vector<uint32_t>(size_t(W) * H, Bg));
    std::puts("PASS hidden in creative (canHurtPlayer false) and under an open panel");

    s.hurt(mcplayer::DamageSource::Generic, 100);
    assert(s.dead());
    auto dead = frame(s, false, false, false);
    const uint32_t top = dead[0], bottom = dead[size_t(H - 1) * W];
    assert(top != Bg && bottom != Bg && top != bottom && ((bottom >> 16) & 255) > ((top >> 16) & 255));
    std::puts("PASS death gradient over the frame (DeathScreen.cpp:45)");
    return 0;
}
