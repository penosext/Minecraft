// Link with renderer_runtime (real World/Inventory); no new native libraries.
// Python3 + Pillow independently decode the pinned PNG, as used by generators.
// Optional argument: path to items.png. Checks stay active under -DNDEBUG.
#include "Inventory.hpp"
#include "InventoryRenderer.hpp"
#include "ItemCatalog.hpp"
#include "ItemIcons.hpp"
#include "LegacyCatalog.hpp"
#include "World.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {
namespace fs = std::filesystem;
using Image = std::vector<uint32_t>;
void check(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}
struct Fixture {
    std::string root;
    mcworld::World world;
    mcinventory::Inventory inventory;
    Fixture() {
        char pattern[] = "/tmp/mc-item-icons-XXXXXX";
        const char* path = ::mkdtemp(pattern); check(path != nullptr, "mkdtemp"); root = path;
        world.create(12345, root, false); inventory.open(world);
    }
    ~Fixture() { std::error_code ec; fs::remove_all(root, ec); }
    void clear() {
        if (!inventory.emptyHand()) check(inventory.consumeSelected(inventory.selectedCount()), "clear selected slot");
    }
};
struct Expected { int id, aux, x, y; const char* name; };
constexpr Expected expected[] = {
    {324,0,176,32,"doorWood"}, {330,0,192,32,"doorIron"},
    {295,0,144,0,"seeds"}, {287,0,128,0,"string"},
    {331,0,128,48,"redstone"}, {338,0,176,16,"reeds"},
    {351,3,224,112,"dyePowder_brown"}, {356,0,96,80,"diode"},
    {372,0,208,112,"netherStalkSeeds"}, {380,0,192,144,"cauldron"},
    {390,0,208,176,"flowerPot"}, {391,0,128,112,"carrots"},
    {392,0,112,112,"potato"},
};
// The original 13 expectations above stay unchanged; these 69 come from
// Item.cpp setIconName plus the ITEM branch of ADD_ICON(row, column, name).
constexpr Expected added[] = {
    {268,0,0,64,"swordWood"}, {272,0,16,64,"swordStone"},
    {267,0,32,64,"swordIron"}, {276,0,48,64,"swordDiamond"},
    {283,0,64,64,"swordGold"},
    {269,0,0,80,"shovelWood"}, {273,0,16,80,"shovelStone"},
    {256,0,32,80,"shovelIron"}, {277,0,48,80,"shovelDiamond"},
    {284,0,64,80,"shovelGold"},
    {270,0,0,96,"pickaxeWood"}, {274,0,16,96,"pickaxeStone"},
    {257,0,32,96,"pickaxeIron"}, {278,0,48,96,"pickaxeDiamond"},
    {285,0,64,96,"pickaxeGold"},
    {271,0,0,112,"hatchetWood"}, {275,0,16,112,"hatchetStone"},
    {258,0,32,112,"hatchetIron"}, {279,0,48,112,"hatchetDiamond"},
    {286,0,64,112,"hatchetGold"},
    {290,0,0,128,"hoeWood"}, {291,0,16,128,"hoeStone"},
    {292,0,32,128,"hoeIron"}, {293,0,48,128,"hoeDiamond"},
    {294,0,64,128,"hoeGold"},
    {298,0,0,0,"helmetCloth"}, {302,0,16,0,"helmetChain"},
    {306,0,32,0,"helmetIron"}, {310,0,48,0,"helmetDiamond"},
    {314,0,64,0,"helmetGold"},
    {299,0,0,16,"chestplateCloth"}, {303,0,16,16,"chestplateChain"},
    {307,0,32,16,"chestplateIron"}, {311,0,48,16,"chestplateDiamond"},
    {315,0,64,16,"chestplateGold"},
    {300,0,0,32,"leggingsCloth"}, {304,0,16,32,"leggingsChain"},
    {308,0,32,32,"leggingsIron"}, {312,0,48,32,"leggingsDiamond"},
    {316,0,64,32,"leggingsGold"},
    {301,0,0,48,"bootsCloth"}, {305,0,16,48,"bootsChain"},
    {309,0,32,48,"bootsIron"}, {313,0,48,48,"bootsDiamond"},
    {317,0,64,48,"bootsGold"},
    {280,0,80,48,"stick"}, {265,0,112,16,"ingotIron"},
    {266,0,112,32,"ingotGold"}, {264,0,112,48,"diamond"},
    {334,0,112,96,"leather"},
    // Plain foods (generate-items.py FOODS): ADD_ICON(row, column) * 16.
    {260,0,160,0,"apple"}, {297,0,144,32,"bread"},
    {319,0,112,80,"porkchopRaw"}, {320,0,128,80,"porkchopCooked"},
    {349,0,144,80,"fishRaw"}, {350,0,160,80,"fishCooked"},
    {357,0,192,80,"cookie"}, {360,0,208,96,"melon"},
    {363,0,144,96,"beefRaw"}, {364,0,160,96,"beefCooked"},
    {366,0,160,112,"chickenCooked"}, {393,0,96,112,"potatoBaked"},
    {396,0,96,144,"carrotGolden"}, {400,0,128,144,"pumpkinPie"},
    {337,0,144,48,"clay"}, {365,0,144,112,"chickenRaw"},
    {263,0,112,0,"coal"}, {263,1,128,160,"charcoal"}, {351,2,224,96,"dyePowder_green"},
};
static_assert(std::size(expected) == 13 && std::size(added) == 69);
constexpr auto allExpected = [] {
    std::array<Expected, 82> result {};
    for (size_t i = 0; i < std::size(expected); ++i) result[i] = expected[i];
    for (size_t i = 0; i < std::size(added); ++i) result[i + 13] = added[i];
    return result;
}();
std::string key(const Expected& e) { return "item:" + std::to_string(e.id) + ":" + std::to_string(e.aux); }

// Decode the actual original PNG, not generated pixels or a second generated
// expectation. No shell, network, source-code imports or persistent temp output.
std::vector<unsigned char> decodePng(const fs::path& png) {
    int descriptors[2]; check(::pipe(descriptors) == 0, "PNG decoder pipe");
    const pid_t pid = ::fork(); check(pid >= 0, "fork PNG decoder");
    if (pid == 0) {
        ::close(descriptors[0]);
        if (::dup2(descriptors[1], STDOUT_FILENO) < 0) ::_exit(126);
        ::close(descriptors[1]);
        const char* script = R"PY(
import hashlib, sys
from PIL import Image
raw = open(sys.argv[1], 'rb').read()
assert hashlib.sha256(raw).hexdigest() == '1562d583262f494526f9ae34047ecbe6af060a237d3e30c751940c239b371064'
assert hashlib.sha1(b'blob ' + str(len(raw)).encode() + b'\0' + raw).hexdigest() == '5456083c02957afa9d7b136980e7513ad5520b2f'
with Image.open(sys.argv[1]) as image:
    assert image.size == (256, 256) and image.mode == 'RGBA'
    sys.stdout.buffer.write(image.tobytes())
)PY";
        ::execlp("python3", "python3", "-c", script, png.c_str(), static_cast<char*>(nullptr));
        ::_exit(127);
    }
    ::close(descriptors[1]);
    std::vector<unsigned char> bytes;
    std::array<unsigned char, 4096> buffer;
    for (;;) {
        const ssize_t n = ::read(descriptors[0], buffer.data(), buffer.size());
        if (n < 0 && errno == EINTR) continue;
        check(n >= 0, "read PNG decoder");
        if (!n) break;
        bytes.insert(bytes.end(), buffer.begin(), buffer.begin() + n);
    }
    ::close(descriptors[0]);
    int status = 0; check(::waitpid(pid, &status, 0) == pid, "wait for PNG decoder");
    check(WIFEXITED(status) && WEXITSTATUS(status) == 0, "Pinned PNG decode failed (requires Python3/Pillow)");
    check(bytes.size() == 256 * 256 * 4, "PNG decoded byte count");
    return bytes;
}
uint32_t texel(const std::vector<unsigned char>& png, const Expected& e, int x, int y) {
    const size_t p = (size_t(e.y + y) * 256 + e.x + x) * 4;
    return uint32_t(png[p+3]) << 24 | uint32_t(png[p]) << 16 | uint32_t(png[p+1]) << 8 | png[p+2];
}
uint32_t over(uint32_t dest, uint32_t src) {
    const uint32_t alpha = src >> 24, oldAlpha = dest >> 24;
    if (!alpha) return dest;
    const uint32_t outAlpha = alpha + (oldAlpha * (255 - alpha) + 127) / 255;
    uint32_t out = outAlpha << 24;
    for (int shift = 0; shift < 24; shift += 8) {
        const uint32_t oldPremul = (((dest >> shift) & 255) * oldAlpha * (255-alpha) + 127) / 255;
        out |= ((((src >> shift) & 255) * alpha + oldPremul + outAlpha/2) / outAlpha) << shift;
    }
    return out;
}
Image render(const mcinventory::Inventory& inventory, int w, int h, bool full, uint32_t background) {
    constexpr uint32_t canary = 0xc1234567u;
    Image pixels(size_t(w) * h + 2, background); pixels.front() = pixels.back() = canary;
    mcinventory::renderInventoryOverlay(pixels.data() + 1, w, h, inventory, full);
    check(pixels.front() == canary && pixels.back() == canary, "Renderer buffer guard");
    return Image(pixels.begin() + 1, pixels.end() - 1);
}
void addExpectedSprite(Image& image, int w, int h, mcinventory::Rect r,
                       const std::vector<unsigned char>& png, const Expected& e) {
    for (int y = std::max(0, r.y); y < std::min(h, r.y+r.h); ++y)
        for (int x = std::max(0, r.x); x < std::min(w, r.x+r.w); ++x) {
            const int sx = (x-r.x)*16/r.w, sy = (y-r.y)*16/r.h;
            image[size_t(y)*w+x] = over(image[size_t(y)*w+x], texel(png, e, sx, sy));
        }
}
void checkPixels(Fixture& f, const std::vector<unsigned char>& png) {
    check(itemicons::kIconCount == 82 && std::size(itemicons::kIcons) == 82 && itemicons::kIconSize == 16,
          "Exact 82-item sprite scope");
    check(!itemicons::findIcon(nullptr) && !itemicons::findIcon("") && !itemicons::findIcon("item:351:0") &&
          !itemicons::findIcon("item:324:1") && !itemicons::findIcon("item:276:1") &&
          !itemicons::findIcon("item:388:0") &&
          !itemicons::findIcon("item:1:0"), "No invented aux/non-catalog/block sprites");
    for (size_t n = 0; n < allExpected.size(); ++n) {
        const auto& e = allExpected[n];
        const std::string canonical = key(e);
        check(std::string(itemicons::kIcons[n].key) == canonical, "Original 13 preserved and new keys appended in source order");
        const auto* icon = itemicons::findIcon(canonical.c_str());
        check(icon && icon->itemId == e.id && icon->aux == e.aux && icon->x == e.x && icon->y == e.y &&
              std::string(icon->name) == e.name, "Sprite identity/crop: " + canonical);
        int transparent = 0, visible = 0;
        for (int y=0; y<16; ++y) for (int x=0; x<16; ++x) {
            const uint32_t source = texel(png, e, x, y);
            check(icon->pixels[y*16+x] == source, "Exact PNG RGBA (including transparent RGB): " + canonical);
            if (source >> 24) ++visible; else ++transparent;
        }
        check(visible && transparent, "Sprite silhouette/alpha: " + canonical);
        const int item = sourceitems::itemIndexForIdAux(e.id, e.aux);
        check(item >= 0 && sourceitems::items()[item].creative, "Original creative item: " + canonical);
        if (std::any_of(std::begin(added), std::end(added), [&](const Expected& a) { return a.id == e.id && a.aux == e.aux; }))
            check(sourceitems::items()[item].renderIndex == -1 && !sourceitems::items()[item].placeable,
                  "New independent item must not become a block: " + canonical);
        for (const auto& dims : {std::pair<int,int>{320,240}, {100,80}, {1,1}}) {
            const int w = dims.first, h = dims.second;
            const auto layout = mcinventory::inventoryLayout(w, h);
            for (bool full : {false, true}) for (uint32_t background : {0xff304050u, 0x40123456u, 0u}) {
                f.clear();
                Image wanted = render(f.inventory, w, h, full, background);
                const Image empty = wanted;
                check(f.inventory.selectHotbarItem(0, item, 1), "Fill creative sprite");
                addExpectedSprite(wanted, w, h, full ? layout.tileSlots[0] : layout.hotbarIcons[0], png, e);
                const Image actual = render(f.inventory, w, h, full, background);
                check(actual == wanted, "Exact PNG/alpha/nearest-neighbor inventory blit: " + canonical);
                if (w == 320 && h == 240 && background == 0xff304050u) {
                    const auto rect = full ? layout.tileSlots[0] : layout.hotbarIcons[0];
                    bool visible = false;
                    for (int y = rect.y; y < rect.y + rect.h; ++y)
                        for (int x = rect.x; x < rect.x + rect.w; ++x)
                            if (actual[size_t(y) * w + x] != empty[size_t(y) * w + x]) visible = true;
                    check(visible, "Sprite visible in inventory overlay: " + canonical);
                }
            }
        }
        // Existing stack-count overlay must still run after the sprite branch.
        if (sourceitems::items()[item].maxStack > 1) {
            f.clear(); check(f.inventory.selectHotbarItem(0, item, 1), "Single sprite");
            const Image single = render(f.inventory, 320, 240, false, 0xff304050u);
            check(f.inventory.selectHotbarItem(0, item, 64), "Stack sprite");
            const Image stack = render(f.inventory, 320, 240, false, 0xff304050u);
            check(stack != single, "Sprite count overlay");
            const auto r = mcinventory::inventoryLayout(320,240).hotbarIcons[0];
            for (int y=0; y<9; ++y) for (int x=0; x<16; ++x)
                check(stack[size_t(r.y+y)*320+r.x+x] == single[size_t(r.y+y)*320+r.x+x], "Count must not shade sprite");
        }
    }
}
void checkCanonicalStates(Fixture& f) {
    int checked = 0;
    for (const auto& e : expected) {
        const std::string canonical = key(e);
        const int item = sourceitems::itemIndexForIdAux(e.id, e.aux);
        check(f.inventory.selectHotbarItem(0, item, 1), "Canonical creative item");
        const Image hotbar = render(f.inventory,320,240,false,0xff304050u);
        const Image panel = render(f.inventory,320,240,true,0xff304050u);
        int variants = 0;
        for (int state=0; state<legacy::kBlockCount; ++state) {
            if (sourceitems::itemForBlockState(state) != item) continue;
            check(!itemicons::findIcon(legacy::kBlockSpecs[state].key), "Never index sprites by world state");
            check(f.inventory.selectHotbarBlock(0, state, 1), "Normalize source world state");
            check(f.inventory.slots()[0].key == canonical, "Canonical key is state-independent");
            check(render(f.inventory,320,240,false,0xff304050u) == hotbar &&
                  render(f.inventory,320,240,true,0xff304050u) == panel,
                  "Door/crop/power state must render the same original item sprite: " + canonical);
            ++variants; ++checked;
        }
        check(variants > 0, "Sprite has canonical source states");
    }
    f.clear(); const Image empty = render(f.inventory,320,240,false,0xff304050u);
    check(f.inventory.selectHotbarBlock(0,"stone",1), "Ordinary item fallback");
    check(render(f.inventory,320,240,false,0xff304050u) != empty, "Unchanged representative mesh fallback");
    std::cout << "Checked " << checked << " canonical world-state aliases against original item sprites\n";
}
} // namespace
int main(int argc, char** argv) {
    try {
        const fs::path png = argc > 1 ? fs::path(argv[1]) : fs::path(__FILE__).parent_path().parent_path() / "port/assets/items/items.png";
        const auto decoded = decodePng(png);
        Fixture f; checkPixels(f,decoded); checkCanonicalStates(f);
        std::cout << "PASS 82 item sprites (13 original + 45 tools/armor + 5 materials + 14 foods + 5 smelting materials): pinned actual PNG/blob, exact RGBA/alpha, creative Inventory, hotbar/panel/scaling/guards, counts and original canonical state invariance\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL item icons: " << error.what() << '\n'; return 1;
    }
}
