// Host-only tests: run tools/extract-inventory.py, then compile Inventory.cpp,
// InventoryRenderer.cpp and existing world/terrain renderer dependencies. No SDK,
// QuickJS, UI or CMake changes required. Assertions must remain enabled.
// -DMCINVENTORY_CORE_ONLY skips all renderer/assets dependencies for core tests.
#include "Inventory.hpp"
#ifndef MCINVENTORY_CORE_ONLY
#include "InventoryRenderer.hpp"
#include "InventoryAssets.hpp"
#endif
#include "World.hpp"
#include "LegacyCatalog.hpp"
#include "ItemCatalog.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;
using Bytes = std::vector<unsigned char>;
using mcinventory::Inventory;
using mcworld::World;
namespace {
struct Temp {
    std::string path;
    Temp() { char pattern[] = "/tmp/mcinventory-test-XXXXXX"; char* result = ::mkdtemp(pattern); assert(result); path = result; }
    ~Temp() { std::error_code ec; fs::remove_all(path, ec); }
};
Bytes read(const std::string& path) {
    std::ifstream in(path, std::ios::binary); assert(in.good());
    return Bytes(std::istreambuf_iterator<char>(in), {});
}
void write(const std::string& path, const Bytes& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc); assert(out.good());
    out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size())); out.close(); assert(out.good());
}
template<class F> void rejects(F fn) {
    bool threw = false; try { fn(); } catch (const std::runtime_error&) { threw = true; } assert(threw);
}
uint32_t hash(const Bytes& bytes, size_t size) {
    uint32_t h = 2166136261u; for (size_t i = 0; i < size; ++i) h = (h ^ bytes[i]) * 16777619u; return h;
}
uint32_t get32(const Bytes& bytes, size_t at) {
    uint32_t result = 0; for (int i = 0; i < 4; ++i) result |= uint32_t(bytes.at(at + i)) << (8 * i); return result;
}
void put32(Bytes& bytes, size_t at, uint32_t value) {
    for (int i = 0; i < 4; ++i) bytes.at(at + i) = static_cast<unsigned char>(value >> (8 * i));
}
void checksum(Bytes& bytes) { put32(bytes, bytes.size() - 4, hash(bytes, bytes.size() - 4)); }
int index(const std::string& key) {
    for (int i = 0; i < legacy::kBlockCount; ++i) if (key == legacy::kBlockSpecs[i].key) return i;
    assert(false); return -1;
}
const sourceitems::ItemSpec* itemForState(int state) {
    const int i = sourceitems::itemForBlockState(state);
    return i >= 0 && i < sourceitems::itemCount() ? sourceitems::items() + i : nullptr;
}
std::string itemKey(const std::string& key) {
    const auto* item = itemForState(index(key)); assert(item); return item->key;
}
struct Fixture {
    Temp root; World world; Inventory inv;
    Fixture() { world.create(12345, root.path, false); inv.open(world); }
    std::string file() const { return world.path() + "/inventory.bin"; }
};
int total(const Inventory& inventory) { int sum = 0; for (const auto& slot : inventory.slots()) sum += slot.count; return sum; }
void lifecycle() {
    Inventory closed; assert(closed.emptyHand()); assert(closed.selectedBlockIndex() == -1);
    World unloaded; rejects([&] { closed.open(unloaded); }); rejects([&] { closed.select(0); });
    Fixture f;
    assert(!fs::exists(f.file())); assert(f.inv.opened() && f.inv.emptyHand());
    assert(f.inv.selectedSlot() == 0 && f.inv.selectedCount() == 0 && total(f.inv) == 0);
    for (const auto& slot : f.inv.armor()) assert(slot.key.empty() && slot.count == 0);
    const std::string empty = f.inv.infoJson();
    assert(empty.find("\"selected\":0") != std::string::npos && empty.find("\"catalogKind\":\"source-items\"") != std::string::npos);
    assert(f.inv.selectHotbarBlock(5, "grass", 64)); assert(f.inv.emptyHand());
    assert(f.inv.select(5)); assert(f.inv.selectedCount() == 64 && f.inv.selectedBlockIndex() == index("grass"));
    assert(f.inv.consumeSelected()); assert(f.inv.selectedCount() == 63);
    const std::string before = f.inv.infoJson();
    for (int s : {-1, 9, std::numeric_limits<int>::max()}) assert(!f.inv.select(s));
    assert(!f.inv.selectHotbarBlock(0, "not-an-item", 64));
    assert(!f.inv.selectHotbarBlock(0, "grass", 0)); assert(!f.inv.selectHotbarBlock(0, "grass", 65));
    assert(!f.inv.consumeSelected(64)); assert(!f.inv.consumeSelected(0)); assert(!f.inv.consumeSelected(-1));
    assert(f.inv.infoJson() == before);
    const Bytes level = read(f.world.path() + "/level.json"), state = read(f.world.path() + "/state.bin"), chunks = read(f.world.path() + "/chunks.bin");
    f.inv.save(f.world); const Bytes bytes = read(f.file());
    assert(bytes.size() == 536 + itemKey("grass").size() && get32(bytes, 8) == 3 && get32(bytes, 32) == 5);
    assert(get32(bytes, 44) == uint32_t(mcinventory::GameMode::Creative));
    assert(f.inv.slots()[5].key == itemKey("grass"));
    assert(f.inv.selectedItemId() == itemForState(index("grass"))->itemId && f.inv.selectedAux() == itemForState(index("grass"))->aux);
    struct stat info {}; assert(!::stat(f.file().c_str(), &info)); assert((info.st_mode & 0777) == 0600 && info.st_nlink == 1);
    Inventory restored; restored.open(f.world); assert(restored.infoJson() == before);
    restored.save(f.world); assert(read(f.file()) == bytes);
    assert(read(f.world.path() + "/level.json") == level && read(f.world.path() + "/state.bin") == state && read(f.world.path() + "/chunks.bin") == chunks);
    assert(restored.consumeSelected(63)); assert(restored.emptyHand() && restored.slots()[5].key.empty());
    restored.save(f.world); restored.open(f.world); assert(restored.emptyHand());
    World other; other.create(12345, f.root.path, false); rejects([&] { restored.save(other); });
}
void stacks() {
    Fixture f;
    assert(f.inv.add(index("stone"), 130)); assert(total(f.inv) == 130);
    assert(f.inv.slots()[0].count == 64 && f.inv.slots()[1].count == 64 && f.inv.slots()[2].count == 2);
    assert(f.inv.transfer(0, 2, 5)); assert(f.inv.slots()[0].count == 59 && f.inv.slots()[2].count == 7);
    assert(f.inv.transfer(1, 2)); assert(f.inv.slots()[1].count == 7 && f.inv.slots()[2].count == 64);
    assert(f.inv.transfer(1, 35)); assert(f.inv.slots()[1].count == 0 && f.inv.slots()[35].count == 7);
    assert(f.inv.selectHotbarBlock(8, "dirt", 3));
    const auto before = f.inv.infoJson();
    assert(!f.inv.transfer(35, 8, 2)); assert(!f.inv.transfer(2, 2)); assert(!f.inv.transfer(-1, 1));
    assert(!f.inv.transfer(1, 0)); assert(!f.inv.transfer(35, 36)); assert(!f.inv.transfer(35, 8, 0));
    assert(!f.inv.transfer(0, 2)); assert(f.inv.infoJson() == before);
    assert(f.inv.transfer(35, 8)); assert(f.inv.slots()[35].key == itemKey("dirt") && f.inv.slots()[8].key == itemKey("stone"));
    assert(f.inv.add(index("stone"), 10)); // Merge before using empty index 1.
    assert(f.inv.slots()[0].count == 64 && f.inv.slots()[8].count == 12 && f.inv.slots()[1].count == 0);
    assert(!f.inv.add(-1, 1)); assert(!f.inv.add(legacy::kBlockCount, 1)); assert(!f.inv.add(0, -1));
    assert(!f.inv.add(0, std::numeric_limits<int>::max()));
    Fixture full; assert(full.inv.add(index("stone"), 36 * 64)); const std::string filled = full.inv.infoJson();
    assert(!full.inv.add(index("stone"), 1)); assert(!full.inv.add(index("dirt"), 1)); assert(full.inv.infoJson() == filled);
    assert(full.inv.consumeSelected(1)); assert(full.inv.add(index("stone"), 1)); assert(full.inv.infoJson() == filled);
    // Every legacy state normalizes to a source item, or is explicitly rejected.
    Fixture all;
    for (int i = 0; i < legacy::kBlockCount; ++i) {
        const auto* item = itemForState(i);
        const auto before = all.inv.infoJson();
        const int count = item ? item->maxStack : 1;
        const bool allowed = item && item->creative;
        assert(all.inv.selectHotbarBlock(i % 9, legacy::kBlockSpecs[i].key, count) == allowed);
        if (!allowed) { assert(all.inv.infoJson() == before); continue; }
        assert(all.inv.select(i % 9)); assert(all.inv.selectedBlockIndex() == item->renderIndex);
        assert(all.inv.slots()[i % 9].key == item->key);
    }
    for (int i = 0; i < sourceitems::itemCount(); ++i) {
        const auto& item = sourceitems::items()[i];
        assert(all.inv.selectHotbarItem(0, i, item.maxStack) == item.creative);
    }
    all.inv.save(all.world); const auto json = all.inv.infoJson(); all.inv.open(all.world); assert(json == all.inv.infoJson());
}
void corruption() {
    Fixture f; assert(f.inv.selectHotbarBlock(0, "stone", 64)); f.inv.save(f.world);
    const Bytes good = read(f.file()); const std::string json = f.inv.infoJson();
    auto invalid = [&](Bytes bad) {
        write(f.file(), bad); rejects([&] { f.inv.open(f.world); }); assert(f.inv.infoJson() == json);
        rejects([&] { f.inv.save(f.world); }); assert(read(f.file()) == bad); write(f.file(), good);
    };
    for (size_t offset : {size_t(0), size_t(8), size_t(12), size_t(16), size_t(20), size_t(24), size_t(28), size_t(32), size_t(36), size_t(40), size_t(44), size_t(48), size_t(52), size_t(56)}) {
        Bytes bad = good; put32(bad, offset, offset == 32 ? 9 : 0xffffffffu); checksum(bad); invalid(bad);
    }
    { Bytes bad = good; bad[60] = 'X'; checksum(bad); invalid(bad); } // Unknown key.
    { Bytes bad = good; put32(bad, 52, 0); checksum(bad); invalid(bad); } // Nonempty key, zero count.
    { Bytes bad = good; put32(bad, 52, 65); checksum(bad); invalid(bad); }
    { Bytes bad = good; put32(bad, 52, 0xffffffffu); checksum(bad); invalid(bad); }
    { Bytes bad = good; put32(bad, 48, 257); checksum(bad); invalid(bad); }
    { Bytes bad = good; put32(bad, 48 + 12 + get32(good, 48) + 4, 1); checksum(bad); invalid(bad); } // Slot 1: empty key, nonzero count.
    { Bytes bad = good; bad[60] = 0; checksum(bad); invalid(bad); } // Embedded NUL is not a key.
    { Bytes bad = good; bad.back() ^= 1; invalid(bad); }
    { Bytes bad = good; bad.push_back(0); invalid(bad); }
    for (size_t n : {size_t(0), size_t(1), size_t(43), good.size() - 1}) invalid(Bytes(good.begin(), good.begin() + n));
    invalid(Bytes(16385, 0));
    // Reserved armor state survives open/save without producing armor stats.
    Bytes armor = good; size_t at = 48;
    for (int i = 0; i < 36; ++i) at += 12 + get32(armor, at);
    const std::string stone = itemKey("stone");
    put32(armor, at, uint32_t(stone.size())); put32(armor, at + 4, 1);
    armor.insert(armor.begin() + at + 12, stone.begin(), stone.end()); checksum(armor);
    write(f.file(), armor); f.inv.open(f.world); assert(f.inv.armor()[0].key == stone);
    f.inv.save(f.world); assert(read(f.file()) == armor);
}
void unsafeFiles() {
    Fixture f; f.inv.save(f.world); const Bytes good = read(f.file());
    auto rejectBoth = [&] { rejects([&] { f.inv.open(f.world); }); rejects([&] { f.inv.save(f.world); }); };
    fs::remove(f.file()); fs::create_symlink("state.bin", f.file()); rejectBoth(); fs::remove(f.file());
    fs::create_directory(f.file()); rejectBoth(); fs::remove(f.file());
    assert(!::mkfifo(f.file().c_str(), 0600)); rejectBoth(); fs::remove(f.file());
    write(f.file(), good); fs::create_hard_link(f.file(), f.world.path() + "/linked"); rejectBoth(); fs::remove(f.world.path() + "/linked");
    fs::remove(f.file()); rejects([&] { f.inv.save(f.world); }); assert(!fs::exists(f.file()));
    write(f.file(), good); f.inv.open(f.world);
    // Valid but externally modified inventory must not be overwritten by stale owner.
    Inventory other; other.open(f.world); assert(other.add(0, 1)); other.save(f.world);
    const Bytes changed = read(f.file()); rejects([&] { f.inv.save(f.world); }); assert(changed == read(f.file()));
    f.inv.open(f.world);
    const std::string level = f.world.path() + "/level.json"; Bytes metadata = read(level);
    Bytes modified = metadata; modified.push_back(' '); write(level, modified);
    rejects([&] { f.inv.save(f.world); }); assert(read(level) == modified); write(level, metadata);
    const std::string state = f.world.path() + "/state.bin"; Bytes stateBytes = read(state);
    fs::remove(state); fs::create_symlink("level.json", state); rejects([&] { f.inv.save(f.world); }); fs::remove(state); write(state, stateBytes);
    const std::string chunks = f.world.path() + "/chunks.bin"; Bytes chunkBytes = read(chunks); Bytes bad = chunkBytes; bad.back() ^= 1;
    write(chunks, bad); rejects([&] { f.inv.save(f.world); }); write(chunks, chunkBytes);
    f.inv.save(f.world);
    const std::string old = f.world.path() + "-old"; fs::rename(f.world.path(), old); fs::create_directory(f.world.path());
    rejects([&] { f.inv.save(f.world); }); assert(!fs::exists(f.file())); fs::remove(f.world.path()); fs::rename(old, f.world.path());
    fs::rename(f.world.path(), old); fs::create_directory_symlink(old, f.world.path()); rejects([&] { f.inv.save(f.world); });
    fs::remove(f.world.path()); fs::rename(old, f.world.path());
    // Replacing the parent while retaining original world inode is also rejected.
    const std::string oldRoot = f.root.path + "-old"; const std::string name = fs::path(f.world.path()).filename();
    fs::rename(f.root.path, oldRoot); fs::create_directory(f.root.path);
    fs::rename(oldRoot + "/" + name, f.world.path()); rejects([&] { f.inv.save(f.world); });
    fs::rename(f.world.path(), oldRoot + "/" + name); fs::remove(f.root.path); fs::rename(oldRoot, f.root.path);
    f.inv.save(f.world);
}
#ifndef MCINVENTORY_CORE_ONLY
void layoutAndPixels() {
    using namespace mcinventory;
    Fixture f;
    const auto l = inventoryLayout(960, 266);
    // The GUI is enlarged in whole quarter steps, so assert the source units
    // scaled by the reported factor rather than hard-coded 1x pixels. Both axes
    // share one scale, so the original texture proportions are preserved.
    const float s = l.scale;
    const auto at = [&](float unit) { return int(std::lround(unit * s)); };
    assert(s > 1 && s <= kMaxGuiScale);
    assert(l.hotbar.w == at(182) && l.hotbar.h == at(22));
    assert(l.hotbar.x == 960 / 2 - at(91) && l.hotbar.y == 266 - at(22));
    assert(l.panel.w == at(176) && l.panel.h == at(166));
    assert(l.panel.x == (960 - at(176)) / 2 && l.panel.y == (266 - at(166)) / 2);
    // Everything must stay inside the viewport at the enlarged scale.
    assert(l.panel.x >= 0 && l.panel.y >= 0);
    assert(l.panel.x + l.panel.w <= 960 && l.panel.y + l.panel.h <= 266);
    assert(l.hotbar.x >= 0 && l.hotbar.x + l.hotbar.w <= 960 && l.hotbar.y + l.hotbar.h <= 266);
    for (int i = 0; i < 9; ++i) {
        assert(l.hotbarIcons[i].x == l.hotbar.x + at(3 + i * assets::SlotPitch));
        assert(l.hotbarIcons[i].y == l.hotbar.y + at(3));
        assert(l.hotbarSelections[i].x == l.hotbar.x + at(-1 + i * assets::SlotPitch));
        assert(l.hotbarSelections[i].y == l.hotbar.y + at(-1));
        // Sizes come from the difference of two scaled edges, not a scaled
        // width, so adjacent slots tile exactly with no seam or overlap.
        const int selectionLeft = -1 + i * assets::SlotPitch;
        assert(l.hotbarSelections[i].w == at(selectionLeft + 24) - at(selectionLeft));
        assert(l.hotbarSelections[i].h == at(-1 + 22) - at(-1));
        assert(l.tileSlots[i].x == l.panel.x + at(assets::GridX + i * assets::GridPitch));
        assert(l.tileSlots[i].y == l.panel.y + at(assets::GridHotbarY));
        assert(l.hitTest(l.hotbarIcons[i].x + 2, l.hotbar.y + at(11), false) == i);
        assert(l.hitTest(l.tileSlots[i].x + 2, l.tileSlots[i].y + 2, true) == i);
    }
    for (int i = 9; i < 36; ++i) {
        const auto r = l.tileSlots[i];
        assert(r.x == l.panel.x + at(assets::GridX + (i % 9) * assets::GridPitch));
        assert(r.y == l.panel.y + at(assets::GridY + ((i - 9) / 9) * assets::GridPitch));
        assert(l.hitTest(r.x + 2, r.y + 2, true) == i);
    }
    assert(l.hitTest(l.armorSlots[0].x + 8, l.armorSlots[0].y + 8, true) == -1);
    assert(l.hitTest(0, 0, false) == -1 && l.hitTest(0, 0, true) == -1);
    std::vector<uint32_t> image(960 * 266, 0xff56789au), original = image;
    renderInventoryOverlay(image.data(), 960, 266, f.inv, false);
    // Enlarging samples the source texture, so compare against the sampled
    // texel for this scale instead of assuming a 1:1 copy. Fully opaque source
    // texels must still be reproduced exactly, with no blending against the
    // background, and nothing may be drawn above the hotbar.
    for (int y = 0; y < l.hotbar.h; ++y) for (int x = 0; x < l.hotbar.w; ++x) {
        const int px = l.hotbar.x + x, py = l.hotbar.y + y;
        // The selected-slot frame is drawn on top of the background, so only
        // compare texels outside it (the original test skipped slot 0 for this).
        if (l.hotbarSelections[f.inv.selectedSlot()].contains(px, py)) continue;
        const int sx = x * assets::HotbarWidth / l.hotbar.w;
        const int sy = y * assets::HotbarHeight / l.hotbar.h;
        const uint32_t source = assets::HotbarPixels[sy * assets::HotbarWidth + sx];
        if ((source >> 24) == 255) assert(image[size_t(py) * 960 + px] == source);
    }
    // The selected frame starts one source unit above the bar, so nothing may be
    // drawn above ITS top edge rather than the hotbar's.
    const int topmost = l.hotbarSelections[0].y;
    assert(topmost < l.hotbar.y);
    assert(std::equal(image.begin(), image.begin() + size_t(topmost) * 960, original.begin()));
    const auto empty = image; assert(f.inv.select(8));
    image = original; renderInventoryOverlay(image.data(), 960, 266, f.inv, false); assert(image != empty);
    const auto withoutItem = image; assert(f.inv.selectHotbarBlock(8, "grass", 1));
    image = original; renderInventoryOverlay(image.data(), 960, 266, f.inv, false); assert(image != withoutItem);
    auto one = image; assert(f.inv.selectHotbarBlock(8, "grass", 64));
    image = original; renderInventoryOverlay(image.data(), 960, 266, f.inv, false); assert(image != one);
    image = original; renderInventoryOverlay(image.data(), 960, 266, f.inv, true); assert(image[0] != original[0]);
    for (int y = 0; y < l.panel.h; ++y) for (int x = 0; x < l.panel.w; ++x) {
        const int sx = x * assets::PanelWidth / l.panel.w;
        const int sy = y * assets::PanelHeight / l.panel.h;
        const uint32_t pixel = assets::PanelPixels[sy * assets::PanelWidth + sx];
        auto icon = l.tileSlots[8];
        icon.w += at(2); icon.h += at(2); // Count anchor/shadow extend beyond the icon.
        const int px = l.panel.x + x, py = l.panel.y + y;
        // The panel figure is drawn inside its own viewport, so background
        // texels there legitimately differ.
        if (icon.contains(px, py) || l.playerView.contains(px, py)) continue;
        if ((pixel >> 24) == 255) assert(image[size_t(py) * 960 + px] == pixel);
    }
    // Source items use their representative thumbnail, never orientation/half keys.
    for (int start = 0; start < sourceitems::itemCount(); start += 9) {
        for (int n = 0; n < 9 && start + n < sourceitems::itemCount(); ++n) {
            const auto& item = sourceitems::items()[start + n];
            assert(f.inv.selectHotbarItem(n, start + n, item.maxStack) == item.creative);
        }
        renderInventoryOverlay(image.data(), 960, 266, f.inv, false);
    }
    for (auto dims : {std::pair<int,int>{1,1}, {100,80}, {183,167}, {184,168}, {320,240}, {961,267}}) {
        const int w = dims.first, h = dims.second; auto small = inventoryLayout(w, h);
        // Below the source 184x168 minimum the fractional fit scale is kept;
        // above it the GUI may enlarge up to the quarter-step cap. Either way
        // the panel must stay inside the viewport.
        const float fit = std::min({kMaxGuiScale, w / 184.0f, h / 168.0f});
        assert(small.scale > 0 && small.scale <= fit + 1e-6f && small.scale <= kMaxGuiScale);
        assert(small.panel.x >= 0 && small.panel.y >= 0);
        assert(small.panel.x + small.panel.w <= w && small.panel.y + small.panel.h <= h);
        std::vector<uint32_t> guarded(size_t(w) * h + 2, 0x12345678u);
        for (bool full : {false, true}) {
            renderInventoryOverlay(guarded.data() + 1, w, h, f.inv, full);
            assert(guarded.front() == 0x12345678u && guarded.back() == 0x12345678u);
        }
    }
    assert(inventoryLayout(0, 100).scale == 0); assert(inventoryLayout(100, -1).scale == 0);
    renderInventoryOverlay(nullptr, 960, 266, f.inv, false);
}
#endif
void originalWorld() {
    Temp root; World world; world.create(987, root.path, true, 864, false);
    Inventory inventory; inventory.open(world); assert(inventory.emptyHand());
    assert(inventory.add(index("grass"), 80)); assert(inventory.select(1)); inventory.save(world);
    const auto json = inventory.infoJson();
    World reopened; reopened.open(world.path(), root.path); Inventory copy; copy.open(reopened); assert(copy.infoJson() == json);
    reopened.save(); copy.save(reopened); // Mutable player/world saves do not invalidate level identity.
}
} // namespace
int main() {
    lifecycle(); stacks(); corruption(); unsafeFiles(); originalWorld();
#ifndef MCINVENTORY_CORE_ONLY
    layoutAndPixels();
    std::cout << "inventory tests passed: state, stacks, corruption, pinned I/O, original worlds, exact layout and all block thumbnails\n";
#else
    std::cout << "inventory core tests passed: state, stacks, corruption, pinned I/O and original worlds (no renderer dependencies)\n";
#endif
}
