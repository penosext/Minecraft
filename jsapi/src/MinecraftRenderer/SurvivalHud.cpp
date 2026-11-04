#include "SurvivalHud.hpp"
#include "HudIcons.hpp"
#include "PlayerSurvival.hpp"

#include <algorithm>
#include <cmath>

namespace mcplayer {
namespace {
int rounded(float value) { return int(std::lround(value)); }
mcinventory::Rect scaled(int x, int y, float s, int ox, int oy) {
    const int left = rounded(x * s), top = rounded(y * s);
    return {ox + left, oy + top, rounded((x + 9) * s) - left, rounded((y + 9) * s) - top};
}
uint32_t blend(uint32_t old, uint32_t pixel) {
    const unsigned a = pixel >> 24;
    if (a == 255) return pixel;
    if (!a) return old;
    uint32_t out = 0xff000000u;
    for (int shift : {0, 8, 16})
        out |= (((pixel >> shift) & 255) * a + ((old >> shift) & 255) * (255 - a) + 127) / 255 << shift;
    return out;
}
void cell(uint32_t* pixels, int w, int h, mcinventory::Rect dest, hudicons::Cell c, int dy = 0) {
    if (dest.w <= 0 || dest.h <= 0) return;
    dest.y += dy;
    for (int y = std::max(0, dest.y); y < std::min(h, dest.y + dest.h); ++y)
        for (int x = std::max(0, dest.x); x < std::min(w, dest.x + dest.w); ++x) {
            const int u = c.u - hudicons::kAtlasX + int(int64_t(x - dest.x) * 9 / dest.w);
            const int v = c.v - hudicons::kAtlasY + int(int64_t(y - dest.y) * 9 / dest.h);
            const size_t at = size_t(y) * w + x;
            pixels[at] = blend(pixels[at], hudicons::kPixels[v * hudicons::kWidth + u]);
        }
}
// java.util.Random, as used by Gui.cpp's random->setSeed(tickCount * 312871).
struct JavaRandom {
    uint64_t seed;
    explicit JavaRandom(int64_t s) : seed((uint64_t(s) ^ 0x5DEECE66DULL) & ((1ULL << 48) - 1)) {}
    int next(int bits) {
        seed = (seed * 0x5DEECE66DULL + 0xBULL) & ((1ULL << 48) - 1);
        return int(int64_t(seed) >> (48 - bits));
    }
    int nextInt(int bound) {
        if ((bound & -bound) == bound) return int((int64_t(bound) * next(31)) >> 31);
        int bits, value;
        do { bits = next(31); value = bits % bound; } while (bits - value + (bound - 1) < 0);
        return value;
    }
};
} // namespace

HudLayout hudLayout(int w, int h) {
    HudLayout result {};
    const auto layout = mcinventory::inventoryLayout(w, h);
    const float s = layout.scale;
    if (s <= 0) return result;
    const int ox = layout.hotbar.x, oy = layout.hotbar.y;
    for (int i = 0; i < 10; ++i) {
        result.hearts[i] = scaled(i * 8, -18, s, ox, oy);
        result.armor[i] = scaled(i * 8, -28, s, ox, oy);
        result.food[i] = scaled(182 - i * 8 - 9, -18, s, ox, oy);
        result.air[i] = scaled(182 - i * 8 - 9, -28, s, ox, oy);
    }
    return result;
}

void renderSurvivalHud(uint32_t* pixels, int w, int h, const Survival& survival,
                       bool creative, bool panelOpen, bool underWater) {
    if (!pixels || w <= 0 || h <= 0 || w > 32768 || h > 32768) return;
    if (survival.dead()) {
        // DeathScreen::render fillGradient(0x60500000 -> 0xa0803030), top to bottom.
        for (int y = 0; y < h; ++y) {
            const float t = h > 1 ? float(y) / float(h - 1) : 0;
            const auto mix = [t](int a, int b) { return unsigned(std::lround(a + (b - a) * t)); };
            const uint32_t color = mix(0x60, 0xa0) << 24 | mix(0x50, 0x80) << 16 | mix(0, 0x30) << 8 | mix(0, 0x30);
            for (int x = 0; x < w; ++x) pixels[size_t(y) * w + x] = blend(pixels[size_t(y) * w + x], color);
        }
        return;
    }
    if (creative || panelOpen) return;
    const HudLayout layout = hudLayout(w, h);
    const int tick = int(survival.tickCount());
    JavaRandom random(int64_t(tick) * 312871);
    // IUIScene_HUD.cpp:196-199.
    const int invulnerable = survival.hurtTicks();
    const bool blink = invulnerable >= 10 && invulnerable / 3 % 2 == 1;
    const int health = int(std::ceil(survival.health()));
    const int oldHealth = int(std::ceil(survival.lastHealth()));
    const int armor = survival.armorValue();
    for (int i = 0; i < 10; ++i) {
        if (armor > 0) {
            cell(pixels, w, h, layout.armor[i], i * 2 + 1 < armor ? hudicons::kArmorFull :
                 i * 2 + 1 == armor ? hudicons::kArmorHalf : hudicons::kArmorEmpty);
        }
    }
    // Hearts drawn from the last index down, like Gui.cpp:474.
    for (int i = 9; i >= 0; --i) {
        const int jitter = health <= 4 ? random.nextInt(2) : 0;
        const int dy = rounded(jitter * mcinventory::inventoryLayout(w, h).scale);
        cell(pixels, w, h, layout.hearts[i], blink ? hudicons::kHeartContainerBlink : hudicons::kHeartContainer, dy);
        if (blink) {
            if (i * 2 + 1 < oldHealth) cell(pixels, w, h, layout.hearts[i], hudicons::kHeartBlinkFull, dy);
            if (i * 2 + 1 == oldHealth) cell(pixels, w, h, layout.hearts[i], hudicons::kHeartBlinkHalf, dy);
        }
        if (i * 2 + 1 < health) cell(pixels, w, h, layout.hearts[i], hudicons::kHeartFull, dy);
        if (i * 2 + 1 == health) cell(pixels, w, h, layout.hearts[i], hudicons::kHeartHalf, dy);
    }
    const int food = survival.food();
    for (int i = 0; i < 10; ++i) {
        int jitter = 0;
        if (survival.saturation() <= 0 && tick % (food * 3 + 1) == 0) jitter = random.nextInt(3) - 1;
        const int dy = rounded(jitter * mcinventory::inventoryLayout(w, h).scale);
        cell(pixels, w, h, layout.food[i], hudicons::kFoodContainer, dy);
        if (i * 2 + 1 < food) cell(pixels, w, h, layout.food[i], hudicons::kFoodFull, dy);
        if (i * 2 + 1 == food) cell(pixels, w, h, layout.food[i], hudicons::kFoodHalf, dy);
    }
    if (underWater) {
        // IUIScene_HUD.cpp:247-249, TOTAL_AIR_SUPPLY 300.
        const int air = survival.air();
        const int count = int(std::ceil((air - 2) * 10.0f / 300));
        const int extra = int(std::ceil(air * 10.0f / 300)) - count;
        for (int i = 0; i < count + extra && i < 10; ++i)
            cell(pixels, w, h, layout.air[i], i < count ? hudicons::kBubble : hudicons::kBubblePop);
    }
}
} // namespace mcplayer
