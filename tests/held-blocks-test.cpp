// First-person held TileItems: every creative item draws something, the source
// canRender shapes take the 3D renderTile path and the rest the extruded-icon
// path (ItemInHandRenderer.cpp:241, TileRenderer.cpp:8512), slab/plate/button/
// snow bounds follow updateDefaultShape, and the per-frame cost stays small.
#include "ActorRenderer.hpp"
#include "ItemCatalog.hpp"
#include "LegacyCatalog.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <tuple>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace {
int failures = 0;
void check(bool ok, const std::string& what)
{
    if (!ok) { ++failures; std::cerr << "FAIL: " << what << '\n'; }
}
constexpr int W = 960, H = 266;
constexpr uint32_t kBackground = 0xff000000u;

// Mirrors JSMinecraftRenderer's held overlay hunk exactly.
struct Held { bool sprite; int index; };
Held route(const sourceitems::ItemSpec& item)
{
    const bool sprite = item.renderIndex < 0 || hasHeldItemSprite(item.itemId, item.aux) ||
                        hasHeldTileSprite(item.itemId, item.aux);
    return {sprite, sprite ? -1 : item.renderIndex};
}
void drawHeld(std::vector<uint32_t>& px, const sourceitems::ItemSpec& item, float swing = 0)
{
    const Held held = route(item);
    renderActorOverlay(px.data(), W, H, 0, 0, swing, held.index);
    if (held.sprite) renderHeldItemOverlay(px.data(), W, H, item.itemId, item.aux, swing);
}
const sourceitems::ItemSpec* item(int id, int aux)
{
    const int i = sourceitems::itemIndexForIdAux(id, aux);
    return i >= 0 ? &sourceitems::kItems[i] : nullptr;
}
FirstPersonDiagnostics diagnose(int index)
{
    FirstPersonState state; state.heldBlockIndex = index;
    FirstPersonDiagnostics d;
    renderOriginalHand(nullptr, W, H, state, &d);
    return d;
}
size_t changed(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b)
{
    size_t n = 0;
    for (size_t i = 0; i < a.size(); ++i) n += a[i] != b[i];
    return n;
}
// Mean colour of the pixels the overlay changed.
std::array<double, 3> meanChanged(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b)
{
    double r = 0, g = 0, bl = 0; size_t n = 0;
    for (size_t i = 0; i < a.size(); ++i) if (a[i] != b[i]) {
        r += (b[i] >> 16) & 255; g += (b[i] >> 8) & 255; bl += b[i] & 255; ++n;
    }
    return n ? std::array<double, 3>{r / n, g / n, bl / n} : std::array<double, 3>{0, 0, 0};
}

void testEveryCreativeItem()
{
    // Catalog shape -> source render shape, see the table in ActorRenderer.cpp.
    const std::set<std::string> block3d = {"cube", "grass", "leaves", "slab", "snow", "button",
        "pressure_plate", "carpet", "piston", "endframe", "chest", "cactus", "stairs", "fence", "wall"};
    const std::set<std::string> sprite = {"cross", "torch", "ladder", "rail", "lever", "pane",
        "vine", "lilypad", "tripwire_hook"};
    const std::vector<uint32_t> background(size_t(W) * H, kBackground);
    std::vector<uint32_t> arm = background;
    renderActorOverlay(arm.data(), W, H, 0, 0, 0, -1);
    int total = 0, tile3d = 0, tileSprite = 0;
    for (int i = 0; i < sourceitems::kItemCount; ++i) {
        const auto& it = sourceitems::kItems[i];
        if (!it.creative) continue;
        ++total;
        std::vector<uint32_t> px = background;
        drawHeld(px, it);
        const Held held = route(it);
        // A sprite is laid over the arm; a held block replaces the arm.
        const size_t drawn = held.sprite ? changed(arm, px) : changed(background, px);
        check(drawn > 0, std::string("held item draws nothing: ") + it.key + " " + it.name);
        if (it.renderIndex < 0 || hasHeldItemSprite(it.itemId, it.aux)) continue;
        const std::string shape = legacy::kBlockSpecs[it.renderIndex].shape;
        const HeldBlockPath path = heldBlockPath(it.renderIndex);
        check(block3d.count(shape) || sprite.count(shape),
              std::string("unclassified held TileItem shape ") + shape + " " + it.key);
        if (block3d.count(shape)) {
            ++tile3d;
            check(path == HeldBlockPath::Block3D && !held.sprite,
                  std::string("canRender shape must use renderTile 3D path: ") + it.key);
            const auto d = diagnose(it.renderIndex);
            check(d.heldBlockSupported && d.heldPath == HeldBlockPath::Block3D && d.triangleCount > 0,
                  std::string("3D diagnostics ") + it.key);
        } else if (sprite.count(shape)) {
            ++tileSprite;
            check(path == HeldBlockPath::Sprite && held.sprite && hasHeldTileSprite(it.itemId, it.aux),
                  std::string("non-canRender shape must use icon sprite path: ") + it.key);
            // renderActorOverlay alone draws nothing for a sprite-path index.
            std::vector<uint32_t> only = background;
            renderActorOverlay(only.data(), W, H, 0, 0, 0, it.renderIndex);
            check(only == background, std::string("sprite index must not draw a 3D guess: ") + it.key);
            const auto d = diagnose(it.renderIndex);
            check(!d.heldBlockSupported && d.heldPath == HeldBlockPath::Sprite,
                  std::string("sprite diagnostics ") + it.key);
        }
    }
    check(total == 194, "creative catalog size " + std::to_string(total));
    check(tileSprite == 18, "sprite TileItems " + std::to_string(tileSprite));
    std::cout << "creative items " << total << ": 3D TileItems " << tile3d << ", icon-sprite TileItems "
              << tileSprite << ", rest items.png\n";
    // Existing items.png contract is unchanged: a flower is a TileItem, not an Item sprite.
    check(!hasHeldItemSprite(37, 0) && hasHeldTileSprite(37, 0), "flower = terrain icon, not items.png");
    check(hasHeldItemSprite(351, 3) && !hasHeldTileSprite(351, 3), "cocoa beans stay items.png");
    check(!hasHeldTileSprite(1, 0) && !hasHeldTileSprite(-1, -1), "stone / empty are not sprites");
    check(heldBlockPath(-1) == HeldBlockPath::None && heldBlockPath(legacy::kBlockCount) == HeldBlockPath::None,
          "out-of-range path");
}

void testBounds()
{
    const auto bounds = [](int id, int aux) { return diagnose(item(id, aux)->renderIndex).rasterBounds; };
    const auto stone = bounds(1, 0), slab = bounds(44, 0), plate = bounds(70, 0),
               button = bounds(77, 0), snow = bounds(78, 0), stairs = bounds(67, 0),
               cobble = bounds(4, 0), frame = bounds(120, 0), chest = bounds(54, 0);
    const auto height = [](const std::array<int, 4>& b) { return b[3] - b[1]; };
    // HalfSlabTile::updateDefaultShape (0,0,0,1,.5,1): the top edge drops, the bottom stays.
    check(slab[1] > stone[1] + 10, "slab top lower than full cube");
    check(std::abs(slab[3] - stone[3]) <= 3, "slab shares the cube's bottom");
    check(height(slab) < height(stone), "slab shorter than cube");
    check(height(snow) < height(slab), "snow layer 2/16 thinner than slab");
    // Centred at y=.5 (not on the floor), so its top sits between cube and slab tops.
    check(plate[1] > stone[1] && plate[1] < slab[1] && height(plate) < height(stone), "pressure plate thin and centred");
    check(height(button) < height(plate) && button[2] - button[0] < stone[2] - stone[0],
          "button smaller than plate");
    check(height(frame) < height(stone) && frame[1] > stone[1], "end frame 13/16 high");
    check(height(chest) < height(stone), "chest 14/16 box");
    // Stairs: same footprint as a cube of the same material, different silhouette.
    check(stairs != cobble, "stairs differ from a full cube");
    std::vector<uint32_t> a(size_t(W) * H, kBackground), b = a;
    drawHeld(a, *item(4, 0)); drawHeld(b, *item(67, 0));
    check(changed(std::vector<uint32_t>(a.size(), kBackground), b) <
          changed(std::vector<uint32_t>(a.size(), kBackground), a), "stairs cover less than cube");
    // Unrotated shapes (stairs/fence/cactus) differ from the BLOCK model-view only by
    // translate(.5) rotate(-90,y) translate(-.5).
    const auto ds = diagnose(item(67, 0)->renderIndex), dc = diagnose(item(4, 0)->renderIndex);
    check(ds.modelView != dc.modelView, "stairs skip renderTile's rotate(90)");
    std::printf("bounds top/bottom: cube %d/%d slab %d/%d snow %d/%d plate %d/%d button %d/%d frame %d/%d\n",
                stone[1], stone[3], slab[1], slab[3], snow[1], snow[3], plate[1], plate[3],
                button[1], button[3], frame[1], frame[3]);
}

void testColours()
{
    const std::vector<uint32_t> background(size_t(W) * H, kBackground);
    std::vector<uint32_t> arm = background;
    renderActorOverlay(arm.data(), W, H, 0, 0, 0, -1);
    const auto spriteMean = [&](int id, int aux) {
        std::vector<uint32_t> px = background;
        drawHeld(px, *item(id, aux));
        return meanChanged(arm, px);
    };
    // Foliage_Default 48b518 (VineTile::getColor(aux), TallGrass::getColor(aux)).
    for (auto [id, aux] : {std::pair{106, 0}, {31, 1}, {31, 2}, {111, 0}}) {
        const auto m = spriteMean(id, aux);
        check(m[1] > m[0] * 1.3 && m[1] > m[2] * 1.3, "green item tint " + std::to_string(id) + ":" + std::to_string(aux));
    }
    // Dead shrub (TallGrass data 0) is NOT tinted; the deadbush icon is brown.
    const auto dead = spriteMean(31, 0);
    check(dead[0] > dead[2], "dead shrub keeps its untinted brown icon");
    // Leaves are 3D with the LeafTile colour; alpha-zero texels stay holes.
    std::vector<uint32_t> leaves(size_t(W) * H, 0x00112233u);
    drawHeld(leaves, *item(18, 0));
    size_t holes = 0, green = 0;
    const auto box = diagnose(item(18, 0)->renderIndex).rasterBounds;
    for (int y = box[1]; y < box[3]; ++y) for (int x = box[0]; x < box[2]; ++x) {
        const uint32_t p = leaves[size_t(y) * W + x];
        if (p == 0x00112233u) ++holes;
        else if (((p >> 8) & 255) > ((p >> 16) & 255)) ++green;
    }
    check(holes > 100 && green > 100, "leaves: cutout holes and foliage tint");
}

double timeIt(const sourceitems::ItemSpec& it, int frames)
{
    std::vector<uint32_t> px(size_t(W) * H, kBackground);
    drawHeld(px, it, 0.3f); // warm caches
    std::vector<double> samples;
    for (int f = 0; f < frames; ++f) {
        const auto start = std::chrono::steady_clock::now();
        drawHeld(px, it, float(f % 20) / 20);
        samples.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    }
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

void benchmark()
{
    std::vector<uint32_t> px(size_t(W) * H, kBackground);
    const auto armStart = std::chrono::steady_clock::now();
    for (int i = 0; i < 100; ++i) renderActorOverlay(px.data(), W, H, 0, 0, 0, -1);
    const double armMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - armStart).count() / 100;
    std::printf("held overlay median ms @%dx%d: arm %.3f", W, H, armMs);
    for (auto [label, id, aux] : {std::tuple{"cube", 1, 0}, {"slab", 44, 0}, {"stairs", 67, 0},
                                  {"fence", 85, 0}, {"leaves", 18, 0}, {"torch(sprite)", 50, 0},
                                  {"flower(sprite)", 37, 0}, {"stick(items.png)", 280, 0}}) {
        const double ms = timeIt(*item(id, aux), 200);
        std::printf(" %s %.3f", label, ms);
        check(ms < 5.0, std::string("held overlay too slow: ") + label);
    }
    std::printf("\n");
}
} // namespace

int main()
{
    const auto coldStart = std::chrono::steady_clock::now();
    (void)heldBlockPath(item(53, 0)->renderIndex); // first use builds that index only
    const double cold = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - coldStart).count();
    std::printf("held model first-use build (one index, once): %.3f ms\n", cold);
    testEveryCreativeItem();
    testBounds();
    testColours();
    benchmark();
    if (failures) { std::cerr << failures << " failure(s)\n"; return 1; }
    std::cout << "PASS held-blocks: every creative item visible, canRender 3D vs icon sprite, "
                 "slab/plate/button/snow/frame bounds, item tints, per-frame cost\n";
    return 0;
}
