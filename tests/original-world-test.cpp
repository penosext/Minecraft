// Link World.cpp + OriginalWorld.cpp with original_decoration, original_structures, original_terrain,
// original_biomes, original_core and pthread. Legacy helpers exercise custom-v1.
#define main legacyWorldRegressionMain
#include "world-test.cpp"
#undef main
#include "OriginalWorld.hpp"
#include "Generator.h"
#include "original_biomes/Source.h"
#include "Structures.h"
#include "Decoration.h"
#include <set>

std::vector<original_terrain::BiomeData> bridge(const std::vector<original_biomes::BiomeData>& input) {
    std::vector<original_terrain::BiomeData> result;
    for (const auto& b : input) result.push_back({b.id, b.depth, b.scale, b.temperature, b.top, b.material});
    return result;
}
size_t overlays(const std::string& path) {
    size_t result = 0;
    for (const auto& e : fs::directory_iterator(path))
        result += e.path().filename().string().find("chunk-") == 0;
    return result;
}
void checkWindow(const mcworld::World& world) {
    const auto volume = world.renderVolume();
    assert(volume.data == world.blockData());
    assert(volume.width == 96 && volume.height == 256 && volume.depth == 96 && volume.originY == 0);
    assert(volume.originX >= 0 && volume.originZ >= 0 && volume.originX + 96 <= world.width() && volume.originZ + 96 <= world.depth());
    assert(world.playerX() - volume.originX >= 40 || volume.originX == 0);
    assert(volume.originX + 96 - world.playerX() >= 40 || volume.originX + 96 == world.width());
    for (int y = 0; y < 256; ++y) for (int z = 0; z < 96; z += 3) for (int x = 0; x < 96; x += 3)
        assert(volume.data[(size_t(y) * 96 + z) * 96 + x] == world.blockAt(x + volume.originX, y, z + volume.originZ));
}
void rawAndCache() {
    mcworld::OriginalWorld world(12345, 864, false, false), other(54321, 864, false, false);
    assert(world.cachedChunkCount() == 0);
    original_biomes::Source source(12345);
    original_terrain::Config config; config.seed = 12345;
    original_terrain::Generator generator(config,
        [&](int x, int z, int w, int h) { return bridge(source.raw(x, z, w, h)); },
        [&](int x, int z, int w, int h) { return bridge(source.zoomed(x, z, w, h)); });
    bool different = false;
    for (const auto& coordinate : {std::pair<int,int>{0,0}, {-1,2}, {-27,-27}, {26,26}}) {
        const auto chunk = generator.generateChunk(coordinate.first, coordinate.second);
        const int ox = (coordinate.first + 27) * 16, oz = (coordinate.second + 27) * 16;
        for (int z = 0; z < 16; ++z) for (int x = 0; x < 16; ++x) {
            assert(world.biomeAt(ox + x, oz + z) == chunk.biomes[size_t(z) * 16 + x].id);
            for (int y = 0; y < 256; ++y) {
                const auto expected = y < 128 ? mcworld::OriginalWorld::catalogForTile(chunk.tiles[original_terrain::Generator::index(x, y, z)]) : 0;
                assert(world.blockAt(ox + x, y, oz + z) == expected);
                if (y < 128) different |= expected != other.blockAt(ox + x, y, oz + z);
            }
        }
    }
    assert(different);
    rejects([&] { mcworld::OriginalWorld::catalogForTile(255); });
    rejects([&] { mcworld::OriginalWorld bad(1, 64); });
    const auto before = world.blockAt(432, 1, 432);
    assert(before && world.setBlock(432, 1, 432, 0));
    for (int chunk = 0; chunk < 100; ++chunk) world.blockAt((chunk % 54) * 16, 10, (chunk / 54) * 16);
    assert(world.cachedChunkCount() <= 64 && world.blockAt(432, 1, 432) == 0);
    const auto a = world.renderVolume(432.5f, 432.5f);
    assert(a.data == world.renderVolume(447.99f, 440).data);
    const auto b = world.renderVolume(448.1f, 432.5f);
    assert(b.originX == a.originX + 16 && world.cachedChunkCount() <= 64);
    assert(world.renderVolume(.25f, .25f).originX == 0);
    assert(world.renderVolume(863.75f, 863.75f).originX == 768);
    const auto edge = world.renderVolume(863.75f, 863.75f);
    assert(edge.data[(size_t(0) * 96 + 95) * 96 + 95] == world.blockAt(863, 0, 863));
}
void worldsAndPersistence() {
    Temp root, outside;
    World a, same, large;
    a.create(12345, root.path, true, 864);
    same.create(12345, root.path, true, 864);
    large.create(12345, root.path); // Original UIStructs default Large, not Classic.
    assert(large.width() == 5120 && large.height() == 256 && large.depth() == 5120 && large.originalGeneration());
    for (int size : {1024, 3072}) {
        World sized; sized.create(7, root.path, true, size);
        World opened; opened.open(sized.path(), root.path);
        assert(opened.width() == size && opened.depth() == size);
        assert(opened.playerX() > size / 2 - 129 && opened.playerX() < size / 2 + 130);
    }
    assert(a.originalGeneration() && a.width() == 864 && a.height() == 256 && a.depth() == 864);
    assert(a.playerX() > 303 && a.playerX() < 562 && a.playerZ() > 303 && a.playerZ() < 562);
    const auto v = a.renderVolume(), s = same.renderVolume();
    assert(std::equal(v.data, v.data + size_t(96) * 256 * 96, s.data));
    assert(!fs::exists(a.path() + "/chunks.bin") && overlays(a.path()) == 0);
    assert(read(a.path() + "/state.bin").size() == 44);
    assert(text(a.path() + "/level.json").find("original-biomes-density-surfaces-structures-decoration-v3") != std::string::npos);
    assert(a.structuresEnabled() && same.structuresEnabled() && large.structuresEnabled());
    assert(a.decorationsEnabled() && same.decorationsEnabled() && large.decorationsEnabled());
    checkWindow(a);
    const auto revision = a.revision(); const auto* pointer = a.blockData();
    a.update(.02f, .1f, 0, false, 0);
    assert(a.revision() == revision && a.blockData() == pointer);
    const int x = int(a.playerX()), z = int(a.playerZ()), y = int(std::round(a.playerY())) - 1;
    assert(a.blockAt(x, y, z) != 0 && a.mine(0, -90));
    assert(a.blockAt(x, y, z) == 0 && a.revision() != revision && a.blockData() == pointer);
    checkWindow(a); a.save();
    assert(overlays(a.path()) == 1 && overlays(same.path()) == 0);
    std::string chunkPath;
    for (const auto& e : fs::directory_iterator(a.path()))
        if (e.path().filename().string().find("chunk-") == 0) chunkPath = e.path();
    assert(read(chunkPath).size() == 296); // 256 biome bytes + one sparse removal.
    World loaded; loaded.open(fs::path(a.path()).filename(), root.path);
    assert(loaded.originalGeneration() && loaded.blockAt(x, y, z) == 0 && loaded.width() == 864);
    near(loaded.playerX(), a.playerX()); near(loaded.playerY(), a.playerY());
    for (int i = 0; i < 8; ++i) loaded.update(.05f, 0, 0, false, 0);
    assert(loaded.playerY() < a.playerY() - .5f); // Runtime-dimension collision dispatch.
    loaded.save(); checkWindow(loaded);
    const auto goodChunk = read(chunkPath), goodState = read(a.path() + "/state.bin");
    const auto goodLevel = read(a.path() + "/level.json");
    auto bad = goodChunk; bad.back() ^= 1; write(chunkPath, bad);
    rejects([&] { a.open(a.path(), root.path); }); rejects([&] { loaded.save(); });
    write(chunkPath, goodChunk); loaded.save();
    fs::rename(chunkPath, chunkPath + ".good"); fs::create_symlink(chunkPath + ".good", chunkPath);
    rejects([&] { a.open(a.path(), root.path); }); rejects([&] { loaded.save(); });
    fs::remove(chunkPath); fs::create_hard_link(chunkPath + ".good", chunkPath);
    rejects([&] { a.open(a.path(), root.path); }); rejects([&] { loaded.save(); });
    fs::remove(chunkPath); fs::rename(chunkPath + ".good", chunkPath);
    fs::create_directory_symlink(root.path, outside.path + "/alias");
    rejects([&] { a.open(outside.path + "/alias/" + fs::path(a.path()).filename().string(), outside.path); });
    rejects([&] { a.open(a.path(), outside.path); });
    fs::create_symlink(a.path() + "/level.json", same.path() + "/fake-level");
    fs::rename(same.path() + "/level.json", same.path() + "/level.good");
    fs::create_symlink(same.path() + "/level.good", same.path() + "/level.json");
    rejects([&] { a.open(same.path(), root.path); });
    fs::remove(same.path() + "/level.json"); fs::rename(same.path() + "/level.good", same.path() + "/level.json");
    bad = goodState; number(bad, 24, std::numeric_limits<float>::quiet_NaN()); u32(bad, 40, hash(bad, 40));
    write(a.path() + "/state.bin", bad); rejects([&] { a.open(a.path(), root.path); });
    write(a.path() + "/state.bin", goodState); write(a.path() + "/level.json", goodLevel);
    // New state/chunks never overwrite other created worlds or legacy files.
    assert(a.path() != same.path() && read(same.path() + "/state.bin").size() == 44);
    World legacy; legacy.create(7, root.path, false); legacy.save();
    World old; old.open(legacy.path(), root.path);
    assert(!old.originalGeneration() && old.width() == 64 && old.height() == 48 && old.depth() == 64);
}
void originalAtomicFailures() {
    Temp root;
    World w; w.create(77, root.path, true, 864);
    const auto initialState = read(w.path() + "/state.bin");
    assert(w.mine(0, -90));
    struct rlimit previous {}; assert(::getrlimit(RLIMIT_FSIZE, &previous) == 0);
    auto limited = previous; limited.rlim_cur = 16;
    const auto signal = std::signal(SIGXFSZ, SIG_IGN);
    assert(::setrlimit(RLIMIT_FSIZE, &limited) == 0);
    rejects([&] { w.save(); });
    assert(::setrlimit(RLIMIT_FSIZE, &previous) == 0); std::signal(SIGXFSZ, signal);
    assert(overlays(w.path()) == 0 && read(w.path() + "/state.bin") == initialState);
    for (const auto& e : fs::directory_iterator(w.path())) assert(e.path().filename().string().find(".tmp-") == std::string::npos);
    w.save(); assert(overlays(w.path()) == 1);
    World reopened; reopened.open(w.path(), root.path);
    const int x = int(w.playerX()), y = int(w.playerY()) - 1, z = int(w.playerZ());
    assert(reopened.blockAt(x, y, z) == 0);
    fs::rename(w.path() + "/state.bin", w.path() + "/state.good");
    fs::create_symlink(w.path() + "/state.good", w.path() + "/state.bin");
    rejects([&] { w.save(); });
    fs::remove(w.path() + "/state.bin"); fs::rename(w.path() + "/state.good", w.path() + "/state.bin");
    fs::rename(w.path(), w.path() + "-moved"); fs::create_directory(w.path());
    rejects([&] { w.save(); });
}
void biomeMetadataAndMaterials() {
    World empty;
    rejects([&] { empty.biomeAt(0, 0); }); rejects([&] { empty.biomeInfo(); });
    rejects([&] { empty.grassColorAt(0, 0); }); rejects([&] { empty.landmarks(); });
    std::set<int> all;
    for (uint32_t seed : {12345u, 54321u, 7u}) {
        mcworld::OriginalWorld world(seed, 864, false, false);
        original_biomes::Source source(seed);
        std::set<int> actual;
        // Sample real cells in separated FINITE chunks; do not demand every
        // biome in every seed, nor use nonexistent out-of-world coordinates.
        for (int z = 8; z < 864; z += 48) for (int x = 8; x < 864; x += 48) {
            const int biome = world.biomeAt(x, z);
            assert(biome == source.zoomed(x - 432, z - 432, 1, 1)[0].id);
            const auto& meta = mcworld::OriginalWorld::biomeMeta(biome);
            assert(meta.id == biome && *meta.name && *meta.sourceName && meta.grassColor && meta.foliageColor);
            actual.insert(biome); all.insert(biome);
        }
        assert(actual.size() > 2 && world.cachedChunkCount() == 0);
        std::cout << "finite-biomes seed=" << seed << " distinct=" << actual.size() << " ids=";
        for (int id : actual) std::cout << id << ',';
        std::cout << '\n';
    }
    assert(all.size() >= 6);
    const auto& plains = mcworld::OriginalWorld::biomeMeta(1);
    assert(std::string(plains.name) == "平原" && std::string(plains.sourceName) == "Plains");
    assert(plains.grassColor == 0x91bd59 && plains.foliageColor == 0x77ab2f);
    assert(mcworld::OriginalWorld::biomeMeta(6).grassColor == 0x5c694e);
    rejects([] { mcworld::OriginalWorld::biomeMeta(23); });
    const auto mapped = [](int tile, int data, const std::string& key) {
        assert(mcworld::OriginalWorld::catalogForTile(uint8_t(tile), uint8_t(data)) == blockId(key));
    };
    for (int data = 0; data < 4; ++data) {
        const std::string wood = std::vector<std::string>{"oak", "spruce", "birch", "jungle"}[data];
        mapped(5, data, "planks_" + wood);
        mapped(17, data, "log_" + wood);
        mapped(17, data + 4, "log_" + wood + "_x");
        mapped(17, data + 8, "log_" + wood + "_z");
    }
    for (int data = 0; data < 3; ++data) { mapped(24, data, "sandstone_" + std::to_string(data)); mapped(31, data, "tallgrass_" + std::to_string(data)); }
    for (int data = 0; data < 8; ++data) {
        mapped(59, data, "wheat_" + std::to_string(data));
        mapped(53, data, "stairs_wood_data_" + std::to_string(data));
        mapped(128, data, "stairs_sandstone_data_" + std::to_string(data));
        mapped(64, data, "door_wood_lower_data_" + std::to_string(data));
        mapped(44, data, "slab_" + std::to_string(data));
        mapped(44, data + 8, "slab_" + std::to_string(data) + "_upper");
    }
    for (int data = 8; data < 12; ++data) mapped(64, data, "door_wood_upper_data_" + std::to_string(data));
    mapped(60, 0, "farmland_0"); mapped(60, 7, "farmland_7");
    // Cocoa facing (data&3) is preserved: direction 0 keeps the per-age key.
    for (int data = 0; data < 12; ++data)
        mapped(127, data, data & 3 ? "cocoa_data_" + std::to_string(data) : "cocoa_" + std::to_string(data >> 2));
    for (int data = 0; data < 12; ++data)
        assert(legacy::kBlockSpecs[mcworld::OriginalWorld::catalogForTile(127, uint8_t(data)) - 1].data == data);
    rejects([] { mcworld::OriginalWorld::catalogForTile(127, 12); });
    // VineTile facing bitmask is preserved per data; data0 (roof) and 15 share
    // the four-sided representative.
    for (int data = 1; data < 15; ++data) mapped(106, data, "vine_data_" + std::to_string(data));
    mapped(106, 0, "vine"); mapped(106, 15, "vine");
    mapped(141, 6, "carrots_2"); mapped(142, 7, "potatoes_3");
    for (const auto& tile : std::vector<std::pair<int, std::string>>{
        {29,"pistonStickyBase"}, {46,"tnt"}, {55,"redStoneDust"}, {69,"lever"},
        {77,"button"}, {93,"diode_off"}, {106,"vine"}, {118,"cauldron"},
        {131,"tripWireSource"}, {132,"tripWire"}, {140,"flowerPot"}}) {
        const auto value = mcworld::OriginalWorld::catalogForTile(uint8_t(tile.first), 3);
        assert(std::string(legacy::kBlockSpecs[value - 1].key).find(tile.second) == 0);
        assert(value != blockId("stone"));
    }
    assert(original_structures::Generator::unsupportedIDs([](uint8_t tile) {
        try { mcworld::OriginalWorld::catalogForTile(tile); return true; } catch (const std::runtime_error&) { return false; }
    }).empty());
    rejects([] { mcworld::OriginalWorld::catalogForTile(5, 4); });
    rejects([] { mcworld::OriginalWorld::catalogForTile(17, 12); });
    mapped(35, 3, "wool_light_blue");
    mapped(35, 15, "wool_black");
    rejects([] { mcworld::OriginalWorld::catalogForTile(35, 16); });
    rejects([] { mcworld::OriginalWorld::catalogForTile(24, 3); });
    Temp root;
    World w; w.create(12345, root.path, true, 864, false, false);
    const auto& current = mcworld::OriginalWorld::biomeMeta(w.biomeAt(int(w.playerX()), int(w.playerZ())));
    const auto info = w.biomeInfo();
    assert(info.find(std::string("\"name\":\"") + current.name + '"') != std::string::npos);
    assert(info.find(std::string("\"sourceName\":\"") + current.sourceName + '"') != std::string::npos);
    assert(w.statusJson().find("\"biome\":" + info) != std::string::npos);
    assert(w.grassColorAt(int(w.playerX()), int(w.playerZ())) == current.grassColor);
    assert(w.foliageColorAt(int(w.playerX()), int(w.playerZ())) == current.foliageColor);
    rejects([&] { w.biomeAt(-1, 0); }); rejects([&] { w.biomeAt(864, 0); });
    World legacy; legacy.create(7, root.path, false);
    assert(legacy.biomeAt(32, 32) == -1 && legacy.biomeInfo().find("\"known\":false") != std::string::npos);
    assert(legacy.biomeInfo().find("grassland/unknown") != std::string::npos);
    assert(legacy.grassColorAt(32, 32) == 0x7cbd6b && legacy.foliageColorAt(32, 32) == 0x5bab46);
    assert(!legacy.structuresEnabled() && legacy.landmarks() == "[]");
}

void generationVersionsAndStructures() {
    Temp root;
    World v1; v1.create(12345, root.path, true, 864, false, false);
    const auto metadata = read(v1.path() + "/level.json");
    assert(!v1.structuresEnabled() && !v1.decorationsEnabled() && v1.landmarks() == "[]");
    assert(text(v1.path() + "/level.json").find("original-biomes-density-surfaces-v1") != std::string::npos);
    const int px = int(v1.playerX()), pz = int(v1.playerZ()), py = int(v1.playerY()) - 1;
    assert(v1.mine(0, -90)); v1.save();
    World reopened; reopened.open(v1.path(), root.path);
    assert(!reopened.structuresEnabled() && reopened.blockAt(px, py, pz) == 0);
    reopened.save(); assert(read(v1.path() + "/level.json") == metadata);
    // Playing and saving a v1 save must never implicitly promote it to the
    // multi-dimension container: no manifest segment, 44-byte state, one
    // dimension, overworld active. See tests/save-v2-test.cpp for the full
    // format coverage; these are the guards that belong with the v1 baseline.
    assert(reopened.saveVersion() == World::SaveVersion::V1);
    assert(!fs::exists(v1.path() + "/dimensions.bin"));
    assert(read(v1.path() + "/state.bin").size() == 44);
    assert(reopened.dimensions().size() == 1);
    assert(reopened.currentDimension() == World::OverworldDimension);
    mcworld::OriginalWorld plain(12345, 864, false, false);
    mcworld::OriginalWorld a(12345, 864, true, false), b(12345, 864, true, false);
    const auto found = a.landmarks(432, 432, 864);
    std::cout << "actual-villages seed=12345 count=" << found.size() << '\n';
    // A stable known Layer seed is required, not a forced Plains callback.
    assert(!found.empty());
    const auto selected = std::find_if(found.begin(), found.end(), [](const auto& v) { return v.type == "Village"; });
    assert(selected != found.end());
    const auto& village = *selected;
    std::cout << "village logical=" << village.x << ',' << village.z << " bounds=" << village.minX << ',' << village.minZ << ',' << village.maxX << ',' << village.maxZ << '\n';
    assert(village.x >= 0 && village.x < 864 && village.z >= 0 && village.z < 864 && village.pieceCount > 2);
    std::set<std::pair<int,int>> touched;
    int carved = 0, built = 0, minedX = -1, minedY = -1, minedZ = -1;
    int roofX = -1, roofY = -1, roofZ = -1;
    const int x0 = std::max(0, village.minX), x1 = std::min(863, village.maxX);
    const int z0 = std::max(0, village.minZ), z1 = std::min(863, village.maxZ);
    // Generate opposite corners first in b; interleaved chunk visitation must
    // still replay identical BASE-relative buildings and cross-chunk carving.
    b.blockAt(x1, 64, z1); b.blockAt(x0, 64, z0);
    for (int cz = z0 / 16; cz <= z1 / 16; ++cz) for (int cx = x0 / 16; cx <= x1 / 16; ++cx) {
        for (int z = cz * 16; z < std::min(864, cz * 16 + 16); ++z) for (int x = cx * 16; x < std::min(864, cx * 16 + 16); ++x)
            for (int y = 1; y < 128; ++y) {
                const auto baseline = plain.blockAt(x, y, z), value = a.blockAt(x, y, z);
                assert(value == b.blockAt(x, y, z));
                if (baseline == value) continue;
                touched.insert({cx, cz});
                if (!value) ++carved;
                else {
                    ++built;
                    if (!baseline && std::strcmp(legacy::kBlockSpecs[value - 1].shape, "liquid")) {
                        if (minedX < 0) { minedX = x; minedY = y; minedZ = z; }
                        if (roofX < 0 && !a.blockAt(x, y + 1, z) && !a.blockAt(x, y + 2, z))
                            { roofX = x; roofY = y; roofZ = z; }
                    }
                }
            }
    }
    assert(touched.size() > 1 && built > 100 && minedX >= 0);
    std::cout << "structure-built=" << built << " carved=" << carved << " chunks=" << touched.size() << '\n';
    assert(a.setBlock(minedX, minedY, minedZ, 0));
    const int key = (minedZ / 16) * 54 + minedX / 16;
    auto changes = a.modifiedChunks().at(key); changes.formatVersion = 1;
    mcworld::OriginalWorld restored(12345, 864, true, false);
    restored.restoreChunk(key, changes);
    assert(restored.blockAt(minedX, minedY, minedZ) == 0);
    mcworld::OriginalWorld wrongVersion(12345, 864, false, false);
    rejects([&] { wrongVersion.restoreChunk(key, changes); }); // air in old BASE, never valid v1 mining
    const auto unmined = b.blockAt(minedX, minedY, minedZ);
    assert(unmined);
    for (int i = 0; i < 90; ++i) {
        restored.blockAt((i % 54) * 16, 32, (i / 54) * 16);
        b.blockAt((i % 54) * 16, 32, (i / 54) * 16);
    }
    assert(b.blockAt(minedX, minedY, minedZ) == unmined); // regenerated structure retry
    assert(restored.cachedChunkCount() <= 64 && restored.blockAt(minedX, minedY, minedZ) == 0);
    assert(roofX >= 0);
    World building; building.create(12345, root.path, true, 864, true, false);
    const auto level = read(building.path() + "/level.json");
    // Fixture places a player on a real village roof; public runtime has no
    // teleport feature. Both halves of a mined building save use the v2 baseline.
    player(building.path(), roofX + .5f, float(roofY + 1), roofZ + .5f);
    World onRoof; onRoof.open(building.path(), root.path);
    assert(onRoof.blockAt(roofX, roofY, roofZ) && onRoof.mine(0, -90));
    onRoof.save();
    assert(overlays(onRoof.path()) == 1 && read(onRoof.path() + "/level.json") == level);
    World remine; remine.open(onRoof.path(), root.path);
    assert(remine.structuresEnabled() && !remine.decorationsEnabled() && remine.blockAt(roofX, roofY, roofZ) == 0);
    for (const auto& e : fs::directory_iterator(remine.path())) {
        if (e.path().filename().string().find("chunk-") == 0) assert(read(e.path()).size() == 296);
    }
    assert(remine.landmarks().find("\"distance\":") != std::string::npos);
    // Generator mismatch is rejected even when an attacker repairs the state
    // metadata checksum: v2 removal in BASE-air cannot become a valid v1 edit.
    std::string changed(level.begin(), level.end());
    const std::string v2 = "original-biomes-density-surfaces-structures-v2", old = "original-biomes-density-surfaces-v1";
    changed.replace(changed.find(v2), v2.size(), old);
    const auto begin = changed.find("\"limitations\":") + std::strlen("\"limitations\":");
    const auto end = changed.find('"', begin + 1);
    changed.replace(begin, end - begin + 1, "\"no-caves-decoration-trees-ores-structures\"");
    writeText(remine.path() + "/level.json", changed);
    auto state = read(remine.path() + "/state.bin");
    u32(state, 16, hash(Bytes(changed.begin(), changed.end()))); u32(state, 40, hash(state, 40));
    write(remine.path() + "/state.bin", state);
    // The chunk's immutable metadata hash is still checked eagerly even when
    // baseline generation for distant edits is deferred.
    rejects([&] { remine.open(remine.path(), root.path); });
    rejects([&] { onRoof.save(); }); // immutable metadata safety retained
}

void v3DecorationAndPredecorParity() {
    // Independent raw TileID/data bridge: compare EVERY cell, including the
    // upper building half. This proves adapter/stage parity, NOT equivalence
    // to the original full BiomeDecorator/postProcess schedule.
    constexpr int size = 864, offset = size / 2;
    original_biomes::Source source(12345);
    original_terrain::Config config; config.seed = 12345; config.worldSize = size;
    original_terrain::Generator terrain(config,
        [&](int x,int z,int w,int h) { return bridge(source.raw(x,z,w,h)); },
        [&](int x,int z,int w,int h) { return bridge(source.zoomed(x,z,w,h)); });
    std::map<std::pair<int,int>, original_terrain::Chunk> bases;
    const auto base = [&](int cx,int cz) -> const original_terrain::Chunk& {
        auto key=std::make_pair(cx,cz); auto it=bases.find(key);
        if(it==bases.end()) it=bases.emplace(key,terrain.generateChunk(cx,cz)).first;
        return it->second;
    };
    const auto baseTile = [&](int x,int y,int z) -> uint8_t {
        if(x < -offset || x >= offset || z < -offset || z >= offset || y<0 || y>=128) return 0;
        const int ax=x+offset, az=z+offset;
        return base(ax/16-27,az/16-27).tiles[original_terrain::Generator::index(ax%16,y,az%16)];
    };
    original_structures::Options options;
    options.generationBiome=[&](int x,int z) { return source.raw(x,z,1,1).at(0).id; };
    original_structures::Generator structures(12345,size,
        [&](int x,int z) { return source.zoomed(x,z,1,1).at(0).id; },
        [&](int x,int z) {
            for(int y=127;y>=0;--y) { const auto tile=baseTile(x,y,z); if(tile && tile!=8 && tile!=9 && tile!=10 && tile!=11 && tile!=18) return y+1; }
            return 0;
        }, baseTile,options);
    using Tile = std::pair<uint8_t,uint8_t>;
    std::map<std::pair<int,int>,std::vector<Tile>> structured;
    const auto predecor = [&](int cx,int cz) -> const std::vector<Tile>& {
        auto key=std::make_pair(cx,cz); auto it=structured.find(key);
        if(it!=structured.end()) return it->second;
        std::vector<Tile> cells(16*256*16);
        for(int y=0;y<128;++y) for(int z=0;z<16;++z) for(int x=0;x<16;++x)
            cells[(size_t(y)*16+z)*16+x]={base(cx,cz).tiles[original_terrain::Generator::index(x,y,z)],0};
        for(const auto& b:structures.generateForChunk(cx,cz))
            cells[(size_t(b.y)*16+(b.z+offset)%16)*16+(b.x+offset)%16]={b.tile,b.data};
        return structured.emplace(key,std::move(cells)).first->second;
    };
    const auto preTile = [&](int x,int y,int z) -> uint8_t {
        if(x < -offset || x >= offset || z < -offset || z >= offset || y<0 || y>=256) return 0;
        return predecor((x+offset)/16-27,(z+offset)/16-27)[(size_t(y)*16+(z+offset)%16)*16+(x+offset)%16].first;
    };
    original_decoration::Generator decorations(12345,size,
        [&](int x,int z) { return source.zoomed(x,z,1,1).at(0).id; },
        [&](int x,int z) {
            for(int y=255;y>=0;--y) { const auto tile=preTile(x,y,z); if(tile && tile!=8 && tile!=9 && tile!=10 && tile!=11 && tile!=18) return y+1; }
            return 0;
        },preTile);
    mcworld::OriginalWorld v2(12345,size,true,false), v3(12345,size,true,true);
    for(const auto& coord:std::vector<std::pair<int,int>>{{0,0},{-14,-16},{-12,-16}}) {
        auto expected=predecor(coord.first,coord.second);
        const int ox=(coord.first+27)*16, oz=(coord.second+27)*16;
        for(int y=0;y<256;++y) for(int z=0;z<16;++z) for(int x=0;x<16;++x) {
            const auto t=expected[(size_t(y)*16+z)*16+x];
            assert(v2.blockAt(ox+x,y,oz+z)==mcworld::OriginalWorld::catalogForTile(t.first,t.second));
        }
        for(const auto& b:decorations.generateForChunk(coord.first,coord.second))
            expected[(size_t(b.y)*16+(b.z+offset)%16)*16+(b.x+offset)%16]={b.tile,b.data};
        for(int y=0;y<256;++y) for(int z=0;z<16;++z) for(int x=0;x<16;++x) {
            const auto t=expected[(size_t(y)*16+z)*16+x];
            assert(v3.blockAt(ox+x,y,oz+z)==mcworld::OriginalWorld::catalogForTile(t.first,t.second));
        }
    }
    std::vector<std::vector<uint16_t>> trees;
    for(uint32_t seed:{12345u,54321u}) {
        mcworld::OriginalWorld world(seed,size,false,true), same(seed,size,false,true);
        const auto window=world.renderVolume(432.5f,432.5f);
        std::vector<uint16_t> signature(size_t(96)*256*96);
        size_t logs=0,leaves=0;
        for(size_t i=0;i<signature.size();++i) if(window.data[i]) {
            const std::string key=legacy::kBlockSpecs[window.data[i]-1].key;
            if(key.find("log_")==0 || key.find("leaves_")==0) {
                signature[i]=window.data[i]; logs+=key.find("log_")==0; leaves+=key.find("leaves_")==0;
            }
        }
        assert(logs>0 && leaves>logs);
        for(int i=0;i<70;++i) same.blockAt((i%54)*16,80,(i/54)*16);
        const auto other=same.renderVolume(432.5f,432.5f);
        assert(std::equal(window.data,window.data+signature.size(),other.data));
        assert(world.cachedChunkCount()<=64 && same.cachedChunkCount()<=64);
        trees.push_back(std::move(signature));
        std::cout << "v3-trees seed=" << seed << " logs=" << logs << " leaves=" << leaves << '\n';
    }
    assert(trees[0]!=trees[1]);
    Temp root;
    World newWorld; newWorld.create(12345,root.path,true,864,false,true);
    assert(newWorld.decorationsEnabled() && !newWorld.structuresEnabled());
    const auto level=read(newWorld.path()+"/level.json");
    World opened; opened.open(newWorld.path(),root.path); opened.save();
    assert(opened.decorationsEnabled() && !opened.structuresEnabled() && read(opened.path()+"/level.json")==level);
    assert(opened.statusJson().find("not-full-BiomeDecorator")!=std::string::npos);
}

int main() {
    rawAndCache(); worldsAndPersistence(); originalAtomicFailures();
    v3DecorationAndPredecorParity();
    biomeMetadataAndMaterials(); generationVersionsAndStructures();
    assert(legacyWorldRegressionMain() == 0);
    std::cout << "original-world-test: v3 seeded trees/decoration subset, complete pre-decoration and decorated chunk parity, finite biome colors, tile metadata, v1/v2 immutable baselines, village/LRU determinism, building mining save/open, four dimensions, render windows, legacy and secure I/O passed\n";
}
