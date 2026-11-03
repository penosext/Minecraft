#include "InventoryRenderer.hpp"
#include "InventoryAssets.hpp"
#include "LegacyBlock.hpp"
#include "LegacyCatalog.hpp"
#include "ItemCatalog.hpp"
#include "ToolRules.hpp"
#include "ItemIcons.hpp"
#include "LegacyTextures.hpp"
#include "SteveSkin.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace mcinventory {
namespace {
constexpr int ThumbnailSize = 32;
// Panel-unit clip box for the reconstructed figure; see renderPanelPlayer.
// Disjoint from every slot rectangle by construction (asserted in tests).
constexpr int kPlayerViewX = 34, kPlayerViewY = 13, kPlayerViewW = 35, kPlayerViewH = 64;
int rounded(float value) { return int(std::lround(value)); }
Rect scaled(int x, int y, int width, int height, float scale, int ox, int oy) {
    const int left = rounded(x * scale), top = rounded(y * scale);
    return {ox + left, oy + top, rounded((x + width) * scale) - left, rounded((y + height) * scale) - top};
}
uint32_t blend(uint32_t old, uint32_t pixel) {
    const unsigned a = pixel >> 24;
    if (a == 255) return pixel;
    if (!a) return old;
    const unsigned oa = old >> 24, outA = a + (oa * (255 - a) + 127) / 255;
    uint32_t result = outA << 24;
    for (int shift : {0, 8, 16}) {
        const unsigned premul = ((pixel >> shift) & 255) * a + (((old >> shift) & 255) * oa * (255 - a) + 127) / 255;
        result |= ((premul + outA / 2) / outA) << shift;
    }
    return result;
}
void blit(uint32_t* pixels, int w, int h, Rect dest, const uint32_t* texture, int tw, int th) {
    if (dest.w <= 0 || dest.h <= 0) return;
    const int left = std::max(0, dest.x), top = std::max(0, dest.y);
    const int right = std::min(w, dest.x + dest.w), bottom = std::min(h, dest.y + dest.h);
    for (int y = top; y < bottom; ++y) for (int x = left; x < right; ++x) {
        const int tx = int(int64_t(x - dest.x) * tw / dest.w), ty = int(int64_t(y - dest.y) * th / dest.h);
        const size_t at = size_t(y) * w + x;
        pixels[at] = blend(pixels[at], texture[ty * tw + tx]);
    }
}
struct Projected { float x, y, z; legacy::Vertex v; };
Projected project(const legacy::Vertex& v) {
    // Explicit platform isometric thumbnail. Not ItemRenderer's GL transform.
    constexpr float cosY = .70710678f, sinY = .70710678f, cosP = .8660254f, sinP = .5f;
    const float x = v.x - .5f, y = v.y - .5f, z = v.z - .5f;
    const float forward = sinY * x + cosY * z;
    return {16 + (cosY * x - sinY * z) * 20,
            16 - (cosP * y - sinP * forward) * 20,
            cosP * forward + sinP * y, v};
}
using Thumbnail = std::array<uint32_t, ThumbnailSize * ThumbnailSize>;
Thumbnail thumbnail(int index) {
    Thumbnail pixels {};
    std::array<float, ThumbnailSize * ThumbnailSize> depths;
    depths.fill(-std::numeric_limits<float>::infinity());
    const auto& mesh = legacy::buildTerrainMesh(index);
    std::vector<size_t> quads;
    for (size_t i = 0; i + 3 < mesh.size(); i += 4) quads.push_back(i);
    const auto quadDepth = [&](size_t i) {
        float value = 0; for (int j = 0; j < 4; ++j) value += project(mesh[i + j]).z; return value;
    };
    std::stable_sort(quads.begin(), quads.end(), [&](size_t a, size_t b) { return quadDepth(a) < quadDepth(b); });
    for (size_t i : quads) {
        Projected p[4] {project(mesh[i]), project(mesh[i + 1]), project(mesh[i + 2]), project(mesh[i + 3])};
        const float ax = p[1].x - p[0].x, ay = p[1].y - p[0].y, bx = p[3].x - p[0].x, by = p[3].y - p[0].y;
        const float det = ax * by - ay * bx;
        if (std::abs(det) < .0001f) continue;
        const int x0 = std::max(0, int(std::floor(std::min({p[0].x, p[1].x, p[2].x, p[3].x}))));
        const int x1 = std::min(ThumbnailSize - 1, int(std::ceil(std::max({p[0].x, p[1].x, p[2].x, p[3].x}))));
        const int y0 = std::max(0, int(std::floor(std::min({p[0].y, p[1].y, p[2].y, p[3].y}))));
        const int y1 = std::min(ThumbnailSize - 1, int(std::ceil(std::max({p[0].y, p[1].y, p[2].y, p[3].y}))));
        for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) {
            const float dx = x + .5f - p[0].x, dy = y + .5f - p[0].y;
            const float a = (dx * by - dy * bx) / det, b = (ax * dy - ay * dx) / det;
            if (a < 0 || a >= 1 || b < 0 || b >= 1) continue;
            const float depth = p[0].z + a * (p[1].z - p[0].z) + b * (p[3].z - p[0].z);
            const size_t offset = size_t(y) * ThumbnailSize + x;
            if (depth + .00001f < depths[offset]) continue;
            auto value = [&](float legacy::Vertex::*field) {
                return p[0].v.*field + a * (p[1].v.*field - p[0].v.*field) + b * (p[3].v.*field - p[0].v.*field);
            };
            const int u = std::clamp(int(value(&legacy::Vertex::u) * legacy::kAtlasWidth), 0, legacy::kAtlasWidth - 1);
            const int v = std::clamp(int(value(&legacy::Vertex::v) * legacy::kAtlasHeight), 0, legacy::kAtlasHeight - 1);
            const uint32_t texel = legacy::kAtlasPixels[v * legacy::kAtlasWidth + u];
            const int alpha = std::clamp(int((texel >> 24) * value(&legacy::Vertex::a)), 0, 255);
            if (!alpha) continue;
            uint32_t shaded = uint32_t(alpha) << 24;
            for (auto channel : {std::pair<int, float legacy::Vertex::*>(16, &legacy::Vertex::r),
                                 {8, &legacy::Vertex::g}, {0, &legacy::Vertex::b}})
                shaded |= uint32_t(std::clamp(int(((texel >> channel.first) & 255) * value(channel.second)), 0, 255)) << channel.first;
            pixels[offset] = blend(pixels[offset], shaded); depths[offset] = depth;
        }
    }
    return pixels;
}
const Thumbnail& cachedThumbnail(int index) {
    // No fixed catalog size and no original GuiItemRenderer dependency. Main thread.
    static std::vector<Thumbnail> cache(legacy::kBlockCount);
    static std::vector<bool> ready(legacy::kBlockCount, false);
    if (!ready[index]) { cache[index] = thumbnail(index); ready[index] = true; }
    return cache[index];
}
void item(uint32_t* pixels, int w, int h, Rect icon, const Slot& slot) {
    if (slot.count < 1 || icon.w < 1 || icon.h < 1) return;
    // Identity is the source Item, not its orientation/half/growth render state.
    // Protected unresolved legacy state never enters the public slot arrays.
    if (const auto* sprite = itemicons::findIcon(slot.key.c_str())) {
        // Original independent Item icon: exact 16x16, no terrain tint/shading.
        blit(pixels, w, h, icon, sprite->pixels, itemicons::kIconSize, itemicons::kIconSize);
    } else {
        const int index = sourceitems::renderIndexForItemKey(slot.key.c_str());
        if (index < 0 || index >= legacy::kBlockCount) return;
        blit(pixels, w, h, icon, cachedThumbnail(index).data(), ThumbnailSize, ThumbnailSize);
    }
    if (slot.count > 1) {
    // Platform 3x5 bitmap digits, white with one-source-pixel black shadow.
    // Original count anchor is x+17-width(amount), y+9 (ItemRenderer.cpp).
    constexpr uint16_t digits[10] = {0x7b6f,0x2492,0x73e7,0x73cf,0x5bc9,0x79cf,0x79ef,0x7249,0x7bef,0x7bcf};
    const std::string text = std::to_string(slot.count);
    auto glyphs = [&](int offset, uint32_t color) {
        for (size_t n = 0; n < text.size(); ++n) {
            const uint16_t bits = digits[text[n] - '0'];
            for (int y = 0; y < 5; ++y) for (int x = 0; x < 3; ++x) if (bits & (1u << (14 - y * 3 - x))) {
                const int gx = 17 - int(text.size()) * 4 + int(n) * 4 + x + offset, gy = 9 + y + offset;
                const int left = icon.x + rounded(float(gx) * icon.w / 16), top = icon.y + rounded(float(gy) * icon.h / 16);
                Rect cell {left, top, rounded(float(gx + 1) * icon.w / 16) - (left - icon.x),
                           rounded(float(gy + 1) * icon.h / 16) - (top - icon.y)};
                blit(pixels, w, h, cell, &color, 1, 1);
            }
        }
    };
    glyphs(1, 0xff000000u); glyphs(0, 0xffffffffu);
    }
    const auto* spec = sourceitems::findItem(slot.key);
    const auto* tool = spec ? mcgame::toolProperties(spec->itemId) : nullptr;
    if (slot.damage <= 0 || !tool || tool->maxDamage <= 0) return;
    // ItemRenderer::renderGuiItemDecorations: source width 13, x+2/y+13,
    // black 13x2, dark 12x1, then rounded remaining width and source RGB.
    // Only the decoration changes; sprite/thumbnail and held icons stay intact.
    const int damage = std::min(slot.damage, tool->maxDamage);
    const int p = int(std::lround(13.0 - double(damage) * 13.0 / tool->maxDamage));
    const int cc = int(std::lround(255.0 - double(damage) * 255.0 / tool->maxDamage));
    const uint32_t ca = 0xff000000u | uint32_t(255 - cc) << 16 | uint32_t(cc) << 8;
    const uint32_t cb = 0xff000000u | uint32_t((255 - cc) / 4) << 16 | uint32_t(255 / 4) << 8;
    auto bar = [&](int width, int height, uint32_t color) {
        const int left = rounded(2.0f * icon.w / 16), top = rounded(13.0f * icon.h / 16);
        const Rect rect {icon.x + left, icon.y + top,
            rounded(float(2 + width) * icon.w / 16) - left,
            rounded(float(13 + height) * icon.h / 16) - top};
        blit(pixels, w, h, rect, &color, 1, 1);
    };
    bar(13, 2, 0xff000000u); bar(12, 1, cb); bar(p, 1, ca);
}
} // namespace

InventoryLayout inventoryLayout(int w, int h) {
    InventoryLayout result;
    if (w <= 0 || h <= 0 || w > 32768 || h > 32768) return result;
    // Enlarge in whole quarter steps so every source GUI unit maps to a whole
    // number of quarter pixels; both axes share one scale, so the original
    // texture aspect is preserved and nothing is stretched. Below the source
    // 184x168 minimum the fractional fit scale is kept, as before.
    const float fit = std::min({kMaxGuiScale, w / 184.0f, h / 168.0f});
    result.scale = fit >= 1.0f ? std::floor(fit * 4.0f) / 4.0f : fit;
    const float s = result.scale;
    result.hotbar = {w / 2 - rounded(91 * s), h - rounded(22 * s), rounded(182 * s), rounded(22 * s)};
    result.panel = {(w - rounded(176 * s)) / 2, (h - rounded(166 * s)) / 2, rounded(176 * s), rounded(166 * s)};
    for (int i = 0; i < 9; ++i) {
        result.hotbarSlots[i] = scaled(1 + i * assets::SlotPitch, 0, 20, 22, s, result.hotbar.x, result.hotbar.y);
        result.hotbarIcons[i] = scaled(3 + i * assets::SlotPitch, 3, 16, 16, s, result.hotbar.x, result.hotbar.y);
        result.hotbarSelections[i] = scaled(-1 + i * assets::SlotPitch, -1, 24, 22, s, result.hotbar.x, result.hotbar.y);
        result.tileSlots[i] = scaled(assets::GridX + i * assets::GridPitch, assets::GridHotbarY, 16, 16, s, result.panel.x, result.panel.y);
        result.tileHitSlots[i] = scaled(assets::GridX - 1 + i * assets::GridPitch, assets::GridHotbarY - 1, 18, 18, s, result.panel.x, result.panel.y);
    }
    for (int y = 0; y < 3; ++y) for (int x = 0; x < 9; ++x) {
        const int i = 9 + y * 9 + x;
        result.tileSlots[i] = scaled(assets::GridX + x * assets::GridPitch, assets::GridY + y * assets::GridPitch, 16, 16, s, result.panel.x, result.panel.y);
        result.tileHitSlots[i] = scaled(assets::GridX - 1 + x * assets::GridPitch, assets::GridY - 1 + y * assets::GridPitch, 18, 18, s, result.panel.x, result.panel.y);
    }
    for (int i = 0; i < 4; ++i) result.armorSlots[i] = scaled(assets::ArmorX, assets::ArmorY + i * assets::ArmorPitch, 16, 16, s, result.panel.x, result.panel.y);
    result.playerView = scaled(kPlayerViewX, kPlayerViewY, kPlayerViewW, kPlayerViewH, s, result.panel.x, result.panel.y);
    return result;
}
int InventoryLayout::hitTest(int x, int y, bool fullInventory) const {
    if (fullInventory) {
        for (int i = 0; i < 36; ++i) if (tileHitSlots[i].contains(x, y)) return i;
    } else {
        for (int i = 0; i < 9; ++i) if (hotbarSlots[i].contains(x, y)) return i;
    }
    return -1;
}
void renderPanelPlayer(uint32_t* pixels, int w, int h, const InventoryLayout& layout) {
    if (!pixels || w <= 0 || h <= 0 || w > 32768 || h > 32768 || layout.scale <= 0) return;
    const Rect clip = layout.playerView;
    if (clip.w <= 0 || clip.h <= 0) return;
    const float s = layout.scale;
    // Source transform parameters, reconstructed (the source block is `#if 0`):
    // anchor (xo+51, yo+75) panel-relative, ss=30 over compile(1/16) model units
    // so one model texel is 30/16 screen units before the GUI scale; glScalef's
    // negative X mirrors the figure; glRotatef(180,0,0,1) plus playerRotY=180
    // leave it upright and facing the viewer. heightOffset=1.62 and ss=30 place
    // the 32-texel-tall model with its feet on the anchor row.
    const float unit = 30.0f / 16.0f * s;
    const float ax = layout.panel.x + 51.0f * s, ay = layout.panel.y + 75.0f * s;
    const auto sx = [&](float mx) { return ax - unit * mx; };
    const auto sy = [&](float my) { return ay - unit * (24.0f - my); };
    // No pointer on this device, so the source atan(xd/40) head/body tracking is
    // deliberately not reconstructed: the pose is the source's zero-offset one.
    // Flat front view; with playerRotY=180 and no rotation only front faces show.
    struct Part { float x0, x1, y0, y1; int u, v, boxW, boxH, boxD; bool mirror; };
    static constexpr Part parts[] = {
        {-4.0f, 4.0f, -8.0f, 0.0f, 0, 0, 8, 8, 8, false},   // head
        {-4.0f, 4.0f, 0.0f, 12.0f, 16, 16, 8, 12, 4, false}, // body
        {-8.0f, -4.0f, 0.0f, 12.0f, 40, 16, 4, 12, 4, false}, // arm0, pos(-5,2)
        {4.0f, 8.0f, 0.0f, 12.0f, 40, 16, 4, 12, 4, true},   // arm1, mirrored
        {-3.9f, 0.1f, 12.0f, 24.0f, 0, 16, 4, 12, 4, false}, // leg0, pos(-1.9,12)
        {-0.1f, 3.9f, 12.0f, 24.0f, 0, 16, 4, 12, 4, true},  // leg1, mirrored
    };
    for (const Part& part : parts) {
        // Model x0 maps to texel u0 (Cube face {1,0,3,2}); the mirrored X scale
        // then puts u0 on the right, so the base case samples U reversed.
        const int left = rounded(sx(part.x1)), right = rounded(sx(part.x0));
        const int top = rounded(sy(part.y0)), bottom = rounded(sy(part.y1));
        if (right <= left || bottom <= top) continue;
        const int u0 = part.u + part.boxD, v0 = part.v + part.boxD;
        for (int y = std::max(clip.y, top); y < std::min(clip.y + clip.h, bottom); ++y) {
            if (y < 0 || y >= h) continue;
            const int ty = v0 + int(int64_t(y - top) * part.boxH / (bottom - top));
            for (int x = std::max(clip.x, left); x < std::min(clip.x + clip.w, right); ++x) {
                if (x < 0 || x >= w) continue;
                int column = int(int64_t(x - left) * part.boxW / (right - left));
                if (!part.mirror) column = part.boxW - 1 - column;
                const uint32_t texel = steve::kSkinPixels[ty * steve::kSkinWidth + u0 + column];
                const size_t at = size_t(y) * w + x;
                pixels[at] = blend(pixels[at], texel);
            }
        }
    }
}
void renderInventoryOverlay(uint32_t* pixels, int w, int h, const Inventory& inventory, bool fullInventory) {
    if (!pixels || w <= 0 || h <= 0 || w > 32768 || h > 32768) return;
    const InventoryLayout layout = inventoryLayout(w, h);
    if (fullInventory) {
        for (size_t i = 0; i < size_t(w) * h; ++i) pixels[i] = blend(pixels[i], 0x90000000u);
        blit(pixels, w, h, layout.panel, assets::PanelPixels, assets::PanelWidth, assets::PanelHeight);
        // Figure first: it is clipped to playerView, which no slot rect overlaps,
        // so slot icons and counts can never be covered by it.
        renderPanelPlayer(pixels, w, h, layout);
        for (int i = 0; i < 36; ++i) item(pixels, w, h, layout.tileSlots[i], inventory.slots()[i]);
        for (int i = 0; i < 4; ++i) item(pixels, w, h, layout.armorSlots[i], inventory.armor()[3 - i]);
        // No invented selected-frame in the full legacy inventory: source only
        // highlights hovered slots, for which this read-only API has no input.
    } else {
        blit(pixels, w, h, layout.hotbar, assets::HotbarPixels, assets::HotbarWidth, assets::HotbarHeight);
        blit(pixels, w, h, layout.hotbarSelections[inventory.selectedSlot()], assets::SelectionPixels,
             assets::SelectionWidth, assets::SelectionHeight);
        for (int i = 0; i < 9; ++i) item(pixels, w, h, layout.hotbarIcons[i], inventory.slots()[i]);
    }
}
} // namespace mcinventory
