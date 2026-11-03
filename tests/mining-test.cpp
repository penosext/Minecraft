/* Standalone host test (no SDK or extra libraries):
   g++ -std=c++17 -O2 -Wall -Wextra -Ijsapi/src/MinecraftRenderer \
       -Ijsapi/build/generated tests/mining-test.cpp \
       jsapi/src/MinecraftRenderer/World.cpp -o /tmp/mining-test && /tmp/mining-test
*/
// Reuse the existing bounded-save fixture helpers, and run its full regression
// suite as well. This keeps deliberate save-file mutations consistent with v1.
#define main worldRegressionMain
#include "world-test.cpp"
#undef main
#include <array>

uint32_t get32(const Bytes& bytes, size_t offset) {
    assert(offset + 4 <= bytes.size());
    uint32_t value = 0;
    for (size_t i = 0; i < 4; ++i) value |= uint32_t(bytes[offset + i]) << (i * 8);
    return value;
}
uint32_t indexOf(int x, int y, int z) { return uint32_t((y * mcworld::Depth + z) * mcworld::Width + x); }
void checksum(Bytes& bytes) { assert(bytes.size() >= 4); u32(bytes, bytes.size() - 4, hash(bytes, bytes.size() - 4)); }
struct MiningFixture {
    Temp root;
    World original;
    std::string path;
    Bytes flat;
    MiningFixture() {
        original.create(901, root.path, false); path = original.path();
        flat = read(path + "/chunks.bin");
        std::fill(flat.begin() + 40, flat.end(), 0);
        for (int z = 0; z < mcworld::Depth; ++z) for (int x = 0; x < mcworld::Width; ++x)
            block(flat, x, 9, z, blockId("stone"));
    }
    void install(const Bytes& chunks, float x = 32.5f, float y = 10, float z = 32.5f) {
        fs::remove(path + "/edits.bin"); synchronize(path, chunks); player(path, x, y, z);
    }
    World load() const { World w; w.open(path, root.path); return w; }
};

void rays() {
    MiningFixture f;
    auto targets = f.flat;
    block(targets, 32, 11, 34, blockId("stone"));
    block(targets, 32, 11, 35, blockId("diamondOre"));
    f.install(targets); World w = f.load();
    const int count = w.blockCount(); const auto revision = w.revision(); const auto* data = w.blockData();
    assert(w.mine(0, 0));
    assert(w.blockAt(32, 11, 34) == 0 && w.blockAt(32, 11, 35) == blockId("diamondOre"));
    assert(w.blockCount() == count - 1 && w.revision() > revision && w.blockData() == data);
    assert(data[indexOf(32, 11, 34)] == 0);
    assert(w.mine(0, 0) && w.blockCount() == count - 2);
    const auto noHitRevision = w.revision();
    assert(!w.mine(0, 0) && w.revision() == noHitRevision);
    w.save(); w.update(.01f, 0, 0, false, 0);
    assert(w.revision() == noHitRevision && w.blockData() == data);

    const std::array<std::array<int, 3>, 4> cells {{{{32, 11, 34}}, {{34, 11, 32}}, {{32, 11, 30}}, {{30, 11, 32}}}};
    for (size_t i = 0; i < cells.size(); ++i) {
        auto chunks = f.flat; const auto& c = cells[i]; block(chunks, c[0], c[1], c[2], blockId("stone"));
        f.install(chunks); w = f.load(); assert(w.mine(float(i * 90), 0)); assert(w.blockAt(c[0], c[1], c[2]) == 0);
    }
    auto chunks = f.flat; block(chunks, 32, 13, 32, blockId("stone"));
    f.install(chunks); w = f.load(); assert(w.mine(1e30f, 90)); assert(w.blockAt(32, 13, 32) == 0);
    f.install(f.flat); w = f.load(); assert(w.mine(0, -90)); assert(w.blockAt(32, 9, 32) == 0);
    // Eye height is feet+1.62, not feet or the top of the player's body.
    chunks = f.flat; block(chunks, 32, 10, 34, blockId("stone"));
    f.install(chunks); w = f.load(); assert(!w.mine(0, 0));
    // A tied X/Z DDA crossing must not mine the voxel only touched at its edge.
    chunks = f.flat; block(chunks, 33, 11, 32, blockId("stone")); block(chunks, 34, 11, 34, blockId("stone"));
    f.install(chunks); w = f.load(); assert(w.mine(45, 0));
    assert(w.blockAt(33, 11, 32) != 0 && w.blockAt(34, 11, 34) == 0);
    // Ray starts exactly on a negative-going cell boundary.
    chunks = f.flat; block(chunks, 31, 11, 32, blockId("stone"));
    f.install(chunks, 33, 10, 32.5f); w = f.load(); assert(w.mine(270, 0));

    for (const char* liquid : {"water", "calmWater", "lava", "calmLava"}) {
        chunks = f.flat; block(chunks, 32, 11, 33, blockId(liquid)); block(chunks, 32, 11, 35, blockId("stone"));
        f.install(chunks); w = f.load(); assert(w.mine(0, 0));
        assert(w.blockAt(32, 11, 33) == blockId(liquid) && w.blockAt(32, 11, 35) == 0);
    }
    chunks = f.flat; block(chunks, 32, 11, 34, blockId("unbreakable")); block(chunks, 32, 11, 35, blockId("stone"));
    f.install(chunks); w = f.load(); const auto bedrockRevision = w.revision(); const auto bedrockCount = w.blockCount();
    assert(!w.mine(0, 0)); assert(w.revision() == bedrockRevision && w.blockCount() == bedrockCount);
    assert(w.blockAt(32, 11, 35) != 0);

    chunks = f.flat; block(chunks, 32, 11, 34, blockId("stone"));
    f.install(chunks); w = f.load(); assert(!w.mine(0, 0, 1.49f)); assert(w.mine(0, 0, 1.5f));
    chunks = f.flat; block(chunks, 32, 11, 38, blockId("stone"));
    f.install(chunks); w = f.load(); assert(!w.mine(0, 0, std::numeric_limits<float>::max()));
    chunks = f.flat; block(chunks, 32, 11, 37, blockId("stone"));
    f.install(chunks, 32.5f, 10, 32); w = f.load(); assert(!w.mine(0, 0, 4.99f)); assert(w.mine(0, 0, 5));
    f.install(f.flat, 63.75f, 10, 32.5f); w = f.load(); assert(!w.mine(90, 0));
    f.install(f.flat, 32.5f, 46.2f, 32.5f); w = f.load(); assert(!w.mine(0, 90));
    const auto validRevision = w.revision();
    for (float bad : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()}) {
        rejects([&] { w.mine(bad, 0); }); rejects([&] { w.mine(0, bad); }); rejects([&] { w.mine(0, 0, bad); });
    }
    rejects([&] { w.mine(0, 0, 0); }); rejects([&] { w.mine(0, 0, -1); });
    assert(!w.mine(std::numeric_limits<float>::max(), std::numeric_limits<float>::max()));
    assert(w.revision() == validRevision);
}

void persistence() {
    MiningFixture f;
    auto chunks = f.flat; block(chunks, 32, 11, 34, blockId("stone")); block(chunks, 34, 11, 32, blockId("stone"));
    f.install(chunks);
    // Reordered saved ids are deliberately different from current catalog ids.
    std::string level = text(f.path + "/level.json"); auto keys = palette(level);
    const size_t stone = size_t(std::find(keys.begin(), keys.end(), "stone") - keys.begin());
    const size_t dirt = size_t(std::find(keys.begin(), keys.end(), "dirt") - keys.begin());
    assert(stone < keys.size() && dirt < keys.size()); std::swap(keys[stone], keys[dirt]);
    palette(level, keys); writeText(f.path + "/level.json", level);
    for (size_t i = 40; i < chunks.size(); i += 2) {
        uint16_t id = uint16_t(chunks[i] | uint16_t(chunks[i + 1]) << 8);
        if (id == stone + 1) id = uint16_t(dirt + 1); else if (id == dirt + 1) id = uint16_t(stone + 1);
        chunks[i] = static_cast<unsigned char>(id); chunks[i + 1] = static_cast<unsigned char>(id >> 8);
    }
    synchronize(f.path, chunks);
    const auto originalChunks = read(f.path + "/chunks.bin"), originalLevel = read(f.path + "/level.json");
    World w = f.load(); const int initialCount = w.blockCount();
    assert(w.mine(0, 0)); assert(w.mine(90, 0)); assert(w.mine(0, -90)); // Deliberately unsorted removal order.
    w.save();
    assert(read(f.path + "/chunks.bin") == originalChunks && read(f.path + "/level.json") == originalLevel);
    const Bytes edits = read(f.path + "/edits.bin");
    assert(edits.size() == 48 && get32(edits, 8) == 1 && get32(edits, 16) == 3);
    assert(get32(edits, 20) < get32(edits, 28) && get32(edits, 28) < get32(edits, 36));
    World loaded = f.load(); assert(snapshot(loaded) == snapshot(w)); assert(loaded.blockCount() == initialCount - 3);
    // Fall into removed terrain: reopening must replay edits before collision.
    for (int i = 0; i < 5; ++i) w.update(.1f, 0, 0, false, 0);
    assert(w.playerY() < 9.8f); w.save(); loaded = f.load(); near(loaded.playerY(), w.playerY());
    loaded.save(); assert(read(f.path + "/edits.bin") == edits);
    assert(read(f.path + "/chunks.bin") == originalChunks && read(f.path + "/level.json") == originalLevel);
    // Removed or changed overlays must not be silently overwritten by an old object.
    fs::remove(f.path + "/edits.bin"); rejects([&] { loaded.save(); });
    write(f.path + "/edits.bin", edits);
    Bytes changed = edits; u32(changed, 20, indexOf(31, 9, 32)); checksum(changed);
    write(f.path + "/edits.bin", changed); rejects([&] { loaded.save(); });
    write(f.path + "/edits.bin", edits); loaded.save();

    // Mining alone marks autosave dirty, without requiring player movement.
    MiningFixture autoFixture; chunks = autoFixture.flat; block(chunks, 32, 11, 34, blockId("stone"));
    autoFixture.install(chunks); World automatic = autoFixture.load(); assert(automatic.mine(0, 0));
    for (int i = 0; i < 105; ++i) automatic.update(.1f, 0, 0, false, 0);
    assert(autoFixture.load().blockAt(32, 11, 34) == 0);
}

void corruptionAndFiles() {
    MiningFixture f;
    auto chunks = f.flat; block(chunks, 32, 11, 34, blockId("stone")); block(chunks, 34, 11, 32, blockId("stone"));
    block(chunks, 31, 11, 34, blockId("unbreakable")); block(chunks, 30, 11, 34, blockId("calmWater"));
    f.install(chunks); World w = f.load(); assert(w.mine(0, 0)); assert(w.mine(90, 0)); w.save();
    const auto good = read(f.path + "/edits.bin"), goodState = read(f.path + "/state.bin");
    const auto preserved = snapshot(w); const auto revision = w.revision(); const auto* data = w.blockData();
    auto invalid = [&](const Bytes& bad) {
        write(f.path + "/edits.bin", bad);
        rejects([&] { w.open(f.path, f.root.path); });
        assert(snapshot(w) == preserved && w.revision() == revision && w.blockData() == data);
        rejects([&] { w.save(); }); assert(read(f.path + "/state.bin") == goodState);
        write(f.path + "/edits.bin", good);
    };
    for (size_t size : {size_t(0), size_t(8), size_t(23), good.size() - 1}) invalid(Bytes(good.begin(), good.begin() + std::ptrdiff_t(size)));
    auto bad = good; bad.push_back(0); invalid(bad);
    bad = good; bad[0] ^= 1; checksum(bad); invalid(bad);
    for (size_t offset : {size_t(8), size_t(12), size_t(16)}) { bad = good; u32(bad, offset, get32(bad, offset) + 1); checksum(bad); invalid(bad); }
    bad = good; bad.back() ^= 1; invalid(bad);
    bad = good; u32(bad, 20, uint32_t(Cells)); checksum(bad); invalid(bad);
    bad = good; u32(bad, 20, UINT32_MAX); checksum(bad); invalid(bad);
    bad = good; u32(bad, 16, UINT32_MAX); checksum(bad); invalid(bad);
    bad = good; u32(bad, 24, 1); checksum(bad); invalid(bad);
    bad = good; u32(bad, 28, get32(bad, 20)); checksum(bad); invalid(bad); // Duplicate.
    bad = good; u32(bad, 20, get32(good, 28)); u32(bad, 28, get32(good, 20)); checksum(bad); invalid(bad); // Unsorted.
    for (uint32_t index : {indexOf(32, 12, 32), indexOf(31, 11, 34), indexOf(30, 11, 34)}) {
        bad = good;
        u32(bad, 20, std::min(index, get32(good, 20))); u32(bad, 28, std::max(index, get32(good, 20)));
        checksum(bad); invalid(bad); // Air, bedrock, liquid; valid ordering/checksum.
    }
    fs::resize_file(f.path + "/edits.bin", 24 + Cells * 8 + 1);
    rejects([&] { w.open(f.path, f.root.path); }); rejects([&] { w.save(); }); write(f.path + "/edits.bin", good);
    fs::rename(f.path + "/edits.bin", f.path + "/edits.good");
    fs::create_symlink(f.path + "/edits.good", f.path + "/edits.bin");
    rejects([&] { w.open(f.path, f.root.path); }); rejects([&] { w.save(); });
    assert(read(f.path + "/edits.good") == good); fs::remove(f.path + "/edits.bin");
    fs::create_hard_link(f.path + "/edits.good", f.path + "/edits.bin");
    rejects([&] { w.open(f.path, f.root.path); }); rejects([&] { w.save(); }); fs::remove(f.path + "/edits.bin");
    fs::create_directory(f.path + "/edits.bin");
    rejects([&] { w.open(f.path, f.root.path); }); rejects([&] { w.save(); }); fs::remove(f.path + "/edits.bin");
    assert(::mkfifo((f.path + "/edits.bin").c_str(), 0600) == 0);
    rejects([&] { w.open(f.path, f.root.path); }); rejects([&] { w.save(); }); fs::remove(f.path + "/edits.bin");
    fs::rename(f.path + "/edits.good", f.path + "/edits.bin");

    // Failed first overlay write leaves immutable terrain, old state, and no temp
    // files. Dirty terrain must be retryable rather than being lost on failure.
    f.install(chunks); w = f.load(); assert(w.mine(0, 0));
    const auto before = read(f.path + "/state.bin"), originalChunks = read(f.path + "/chunks.bin");
    struct rlimit previous {}; assert(::getrlimit(RLIMIT_FSIZE, &previous) == 0);
    struct rlimit limited = previous; limited.rlim_cur = 16;
    auto oldSignal = std::signal(SIGXFSZ, SIG_IGN); assert(::setrlimit(RLIMIT_FSIZE, &limited) == 0);
    rejects([&] { w.save(); }); assert(::setrlimit(RLIMIT_FSIZE, &previous) == 0); std::signal(SIGXFSZ, oldSignal);
    assert(!fs::exists(f.path + "/edits.bin"));
    assert(read(f.path + "/state.bin") == before && read(f.path + "/chunks.bin") == originalChunks);
    for (const auto& e : fs::directory_iterator(f.path)) assert(e.path().filename().string().find(".tmp-") == std::string::npos);
    w.save(); const auto firstEdit = read(f.path + "/edits.bin"); assert(f.load().blockAt(32, 11, 34) == 0);
    assert(w.mine(90, 0));
    assert(::setrlimit(RLIMIT_FSIZE, &limited) == 0); oldSignal = std::signal(SIGXFSZ, SIG_IGN);
    rejects([&] { w.save(); }); assert(::setrlimit(RLIMIT_FSIZE, &previous) == 0); std::signal(SIGXFSZ, oldSignal);
    assert(read(f.path + "/edits.bin") == firstEdit); w.save(); assert(f.load().blockAt(34, 11, 32) == 0);
    // Overlay commits (32 bytes) but state fails (44 bytes): retry must accept
    // the just-committed overlay, not mistake it for an external modification.
    f.install(chunks); w = f.load(); assert(w.mine(0, 0)); limited.rlim_cur = 36;
    oldSignal = std::signal(SIGXFSZ, SIG_IGN); assert(::setrlimit(RLIMIT_FSIZE, &limited) == 0);
    rejects([&] { w.save(); }); assert(::setrlimit(RLIMIT_FSIZE, &previous) == 0); std::signal(SIGXFSZ, oldSignal);
    assert(read(f.path + "/edits.bin").size() == 32 && f.load().blockAt(32, 11, 34) == 0);
    w.save(); assert(f.load().blockAt(32, 11, 34) == 0);
}

int main() {
    assert(worldRegressionMain() == 0);
    rays(); persistence(); corruptionAndFiles();
    std::cout << "mining-test: DDA, reach, bedrock/liquids, cache interfaces, immutable terrain, overlay replay/palette remap, corruption, confinement, autosave and atomic-write failures passed\n";
}
