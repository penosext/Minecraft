// Standalone native regression. Link against world_runtime and original modules;
// intentionally not registered in CMake (the integration owner controls it).
#define main legacyWorldHelpersMain
#include "world-test.cpp"
#undef main
#include "OriginalWorld.hpp"
#include "Scene.hpp"
#include "ChunkPump.hpp"
#include <chrono>
#include <thread>

using mcworld::OriginalWorld;
size_t local(int x, int y, int z) { return (size_t(y) * 16 + z) * 16 + x; }
void append32(Bytes& bytes, uint32_t value) {
    for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<unsigned char>(value >> (8 * i)));
}
Bytes overlay(const World& world, int cx, int cz, const OriginalWorld::Edits& edits) {
    Bytes bytes {'M','C','O','C','H','N','K',0};
    const int key = cz * (world.width() / 16) + cx;
    append32(bytes, 2); append32(bytes, world.seed());
    append32(bytes, hash(read(world.path() + "/level.json"))); append32(bytes, uint32_t(key));
    append32(bytes, uint32_t(edits.size()));
    for (int z = 0; z < 16; ++z) for (int x = 0; x < 16; ++x)
        bytes.push_back(uint8_t(world.biomeAt(cx * 16 + x, cz * 16 + z)));
    // Newly created saves have the immutable CURRENT catalog ordering.
    for (const auto& edit : edits) { append32(bytes, edit.first); append32(bytes, edit.second); }
    append32(bytes, hash(bytes)); return bytes;
}
std::string chunkFile(const World& world, int cx, int cz) {
    return world.path() + "/chunk-" + std::to_string(cz * (world.width() / 16) + cx) + ".bin";
}
void drain(World& world) {
    while (world.streamingInfo().queued) {
        const auto before = world.streamingInfo();
        assert(world.processChunkLoads(1) == 1);
        const auto after = world.streamingInfo();
        assert(after.totalGenerated == before.totalGenerated + 1);
        assert(after.loaded == before.loaded + 1 && after.queued == before.queued - 1);
    }
}
void rawStreaming() {
    OriginalWorld raw(12345, 864, true, true);
    raw.beginStreaming(1); raw.setStreamingCenter(432.5f, 432.5f);
    assert(raw.streamingInfo().target == 9 && raw.streamingInfo().queued == 9);
    assert(raw.streamingInfo().preloadRadius == 1 && raw.streamingInfo().displayTarget == 9);
    assert(raw.blockAt(432, 0, 432) == 0 && raw.blockAt(80, 64, 80) == 0);
    assert(!raw.setBlock(80, 70, 80, blockId("stone")));
    raw.biomeAt(80, 80);
    const auto empty = raw.renderVolume(432.5f, 432.5f);
    assert(empty.width == 48 && empty.height == 256 && empty.depth == 48);
    assert(std::all_of(empty.data, empty.data + size_t(48) * 256 * 48, [](uint16_t v) { return !v; }));
    assert(raw.streamingInfo().totalGenerated == 0);
    rejects([&] { raw.setRenderRadius(0); }); rejects([&] { raw.setRenderRadius(13); });
    rejects([&] { raw.processChunkLoads(-1); });
    rejects([&] { raw.setStreamingCenter(std::numeric_limits<float>::max(), 0); });
    rejects([&] { raw.setStreamingCenter(-.5f, 0); });
    assert(raw.processChunkLoads(0) == 0 && raw.processChunkLoads(1) == 1);
    auto one = raw.streamingInfo();
    assert(one.loaded == 1 && one.queued == 8 && one.resident == 1 && one.totalGenerated == 1);
    assert(raw.chunkLoaded(432, 432) && !raw.chunkLoaded(416, 416)); // center first
    assert(one.baseCached > 0 && one.structureCached > 0 && raw.cachedChunkCount() <= 64);
    const auto revision = raw.streamingRevision();
    raw.renderVolume(432.5f, 432.5f); raw.blockAt(80, 5, 80); raw.biomeAt(80, 80);
    raw.landmarks(432, 432); // explicit query may use dependency caches, not FINAL
    assert(raw.streamingRevision() == revision && raw.streamingInfo().totalGenerated == 1);
    assert(raw.processChunkLoads(2) == 2);
    assert(raw.streamingInfo().loaded == 3 && raw.streamingInfo().totalGenerated == 3);
    raw.setStreamingCenter(752.5f, 752.5f); // cancels all old queued work
    auto moved = raw.streamingInfo();
    assert(moved.loaded == 0 && moved.resident == 0 && moved.queued == 9);
    assert(!raw.chunkLoaded(432, 432) && raw.blockAt(432, 1, 432) == 0);
    assert(raw.processChunkLoads(1) == 1 && raw.chunkLoaded(752, 752));
    assert(!raw.chunkLoaded(416, 432));
    raw.setRenderRadius(3);
    assert(raw.streamingInfo().target == 49 && raw.streamingInfo().queued == 48);
    while (raw.streamingInfo().queued) assert(raw.processChunkLoads(1) == 1);
    assert(raw.streamingInfo().loaded == 49 && raw.streamingInfo().resident == 49 && raw.cachedChunkCount() <= 64);
    const auto volume = raw.renderVolume(752.5f, 752.5f);
    assert(volume.width == 112 && volume.height == 256 && volume.depth == 112);
    assert(volume.originX % 16 == 0 && volume.originZ % 16 == 0);
    const int editX = volume.originX + 108, editZ = volume.originZ + 109;
    assert(raw.blockAt(editX, 200, editZ) == 0 && raw.setBlock(editX, 200, editZ, blockId("stone")));
    assert(volume.data[(size_t(200) * 112 + 109) * 112 + 108] == blockId("stone"));
    assert(raw.renderVolume(752.5f, 752.5f).data == volume.data);
    assert(raw.setBlock(editX, 200, editZ, 0));
    assert(volume.data[(size_t(200) * 112 + 109) * 112 + 108] == 0);
    const auto count = raw.streamingInfo().totalGenerated;
    raw.setRenderRadius(1);
    assert(raw.streamingInfo().target == 9 && raw.streamingInfo().loaded == 9 && raw.streamingInfo().resident == 9);
    assert(raw.processChunkLoads(100) == 0 && raw.streamingInfo().totalGenerated == count);
    raw.renderVolume(.25f, .25f);
    assert(raw.streamingInfo().queued == 9 && raw.streamingInfo().resident == 0);
    const auto edge = raw.renderVolume(863.75f, 863.75f);
    assert(edge.originX == 816 && edge.originZ == 816 && edge.width == 48);
    assert(raw.processChunkLoads(1) == 1 && raw.chunkLoaded(863, 863));
    std::cout << "stream-step v3 lastLoadMs=" << raw.streamingInfo().lastLoadMs << '\n';
}
void worldStreamingAndScene() {
    Temp root;
    World world; world.beginStreaming(1);
    std::vector<mcworld::CreateProgress> events;
    world.create(12345, root.path, true, 864, false, false, [&](const auto& p) {
        events.push_back(p);
        if (p.stage == "post") assert(world.loaded() && fs::exists(world.path() + "/state.bin"));
    });
    assert(events.front().stage == "spawn" && events.back().stage == "post" && events.back().done == 1);
    assert(std::any_of(events.begin(), events.end(), [](const auto& p) { return p.stage == "initialsave" && p.done == p.total; }));
    auto info = world.streamingInfo();
    assert(info.enabled && info.radius == 1 && info.windowSize == 48 && info.maxDistance == 8 && info.target == 9);
    assert(info.loaded >= 1 && info.loaded + info.queued == 9);
    const auto generated = info.totalGenerated, revision = world.revision();
    for (int i = 0; i < 3; ++i) {
        world.renderVolume(); world.statusJson(); world.biomeInfo(); world.landmarks();
        assert(world.blockAt(16, 64, 16) == 0);
    }
    mcscene::Scene scene; scene.open(world);
    for (int i = 0; i < 20; ++i) scene.update(world, .05f);
    assert(world.streamingInfo().totalGenerated == generated && world.revision() == revision);
    assert(world.processChunkLoads(0) == 0 && world.processChunkLoads(1) == 1);
    assert(world.revision() != revision);
    const auto readyRevision = world.revision(); world.renderVolume();
    assert(world.revision() == readyRevision);
    drain(world);
    assert(world.streamingInfo().totalGenerated == 9 && world.streamingInfo().loaded == 9);
    for (int radius : {2, 3, 1}) {
        world.setRenderRadius(radius); drain(world);
        const int size = (radius * 2 + 1) * 16;
        const auto v = world.renderVolume();
        assert(v.width == size && v.depth == size && v.height == 256);
        assert(v.originX >= 0 && v.originX + size <= world.width() && v.originX % 16 == 0);
        assert(world.streamingInfo().maxDistance == radius * 16 - 8);
        const auto ranges = world.streamingInfo();
        assert(ranges.radius == radius && ranges.preloadRadius == radius);
        assert(ranges.target == (radius * 2 + 1) * (radius * 2 + 1));
        assert(ranges.displayTarget == ranges.target && ranges.displayLoaded == ranges.loaded);
        const int x = int(world.playerX()), z = int(world.playerZ()), y = int(world.playerY()) - 1;
        const size_t at = (size_t(y) * size + z - v.originZ) * size + x - v.originX;
        assert(v.data[at] == world.blockAt(x, y, z));
    }
    const auto v = world.renderVolume();
    assert(world.mine(0, -90));
    const int x = int(world.playerX()), z = int(world.playerZ()), y = int(world.playerY()) - 1;
    assert(v.data[(size_t(y) * v.depth + z - v.originZ) * v.width + x - v.originX] == 0);
    assert(world.streamingInfoJson().find("\"lastLoadMs\":") != std::string::npos);
    world.save();
    World opened; opened.beginStreaming(1); opened.open(world.path(), root.path);
    assert(opened.blockAt(x, y, z) == 0 && opened.streamingInfo().target == 9);
    opened.endStreaming(); assert(opened.chunkLoaded(16, 16));
    assert(!opened.chunkLoaded(-1, 0));
    World legacy; legacy.create(7, root.path, false);
    legacy.beginStreaming(1); assert(legacy.processChunkLoads(1) == 0 && legacy.chunkLoaded(1, 1));
}
void physicsUnknownBoundary() {
    Temp root;
    World fixture; fixture.create(12345, root.path, true, 864, false, false);
    const int cx = int(fixture.playerX()) / 16, cz = int(fixture.playerZ()) / 16;
    OriginalWorld::Edits floor;
    for (int z = 0; z < 16; ++z) for (int x = 0; x < 16; ++x) floor[uint32_t(local(x, 179, z))] = blockId("stone");
    write(chunkFile(fixture, cx, cz), overlay(fixture, cx, cz, floor));
    const float boundary = float((cx + 1) * 16);
    player(fixture.path(), boundary - 1.0f, 180, cz * 16 + 8.5f);
    World w; w.beginStreaming(1); w.open(fixture.path(), root.path);
    assert(w.streamingInfo().loaded == 1 && w.streamingInfo().totalGenerated == 1);
    const auto before = w.streamingInfo().totalGenerated;
    for (int i = 0; i < 40; ++i) w.update(.05f, 1, 0, false, 90);
    assert(w.playerX() <= boundary - .249f && w.playerX() >= boundary - .251f);
    near(w.playerY(), 180);
    assert(!w.pick(90, 0).hit && !w.mine(90, 0) && !w.place(90, 0, blockId("stone") - 1));
    assert(w.streamingInfo().totalGenerated == before && !w.chunkLoaded(int(boundary), int(w.playerZ())));
    // Once queued work becomes ready, the invisible wall is released.
    while (!w.chunkLoaded(int(boundary), int(w.playerZ()))) assert(w.processChunkLoads(1) == 1);
    w.update(.05f, 1, 0, false, 90);
    assert(w.playerX() > boundary - .249f);
    w.update(.1f, 1, 0, false, 90);
    assert(w.playerX() > boundary && w.streamingInfo().centerChunkX == cx + 1);
    assert(w.streamingInfo().target == 9 && w.streamingInfo().loaded + w.streamingInfo().queued == 9);
    drain(w);
    assert(!w.chunkLoaded((cx - 1) * 16, cz * 16)); // old-side queue really cancelled
    assert(w.chunkLoaded((cx + 2) * 16, cz * 16));
}
void deferredSavedChunks() {
    Temp root;
    World fixture; fixture.create(12345, root.path, true, 864, false, false);
    const int cx = 5, cz = 5, x = 80, z = 80;
    int y = 1;
    while (y < 127 && (!fixture.blockAt(x, y, z) || std::string(legacy::kBlockSpecs[fixture.blockAt(x, y, z) - 1].key) == "unbreakable")) ++y;
    assert(y < 127);
    OriginalWorld::Edits edits {{uint32_t(local(0, y, 0)), 0}};
    const auto bytes = overlay(fixture, cx, cz, edits);
    const auto file = chunkFile(fixture, cx, cz); write(file, bytes);
    World opened; opened.beginStreaming(1);
    const auto start = std::chrono::steady_clock::now();
    opened.open(fixture.path(), root.path);
    const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    assert(opened.streamingInfo().deferred == 1 && opened.streamingInfo().totalGenerated == 1);
    assert(!opened.chunkLoaded(x, z) && opened.blockAt(x, y, z) == 0);
    opened.save(); assert(read(file) == bytes && opened.streamingInfo().deferred == 1);
    const auto spawnState = read(fixture.path() + "/state.bin");
    player(fixture.path(), x + .5f, 200, z + .5f);
    World far; far.beginStreaming(1); far.open(fixture.path(), root.path);
    assert(far.streamingInfo().deferred == 0 && far.blockAt(x, y, z) == 0);
    assert(far.streamingInfo().totalGenerated == 1 && far.streamingInfo().loaded == 1);
    write(fixture.path() + "/state.bin", spawnState);
    auto bad = bytes;
    // Redundant baseline edit, valid file checksum: open distant succeeds, load throws.
    u32(bad, 288, fixture.blockAt(x, y, z)); u32(bad, bad.size() - 4, hash(bad, bad.size() - 4));
    write(file, bad);
    World deferred; deferred.beginStreaming(1); deferred.open(fixture.path(), root.path);
    assert(deferred.streamingInfo().deferred == 1);
    player(fixture.path(), x + .5f, 200, z + .5f);
    rejects([&] { World invalid; invalid.beginStreaming(1); invalid.open(fixture.path(), root.path); });
    write(fixture.path() + "/state.bin", spawnState);
    // Baseline-independent bedrock and air-removal constraints are also delayed.
    bad = bytes; u32(bad, 284, 0); u32(bad, bad.size() - 4, hash(bad, bad.size() - 4));
    write(file, bad); World bedrock; bedrock.beginStreaming(1); bedrock.open(fixture.path(), root.path);
    player(fixture.path(), x + .5f, 200, z + .5f);
    rejects([&] { World invalid; invalid.open(fixture.path(), root.path); });
    write(fixture.path() + "/state.bin", spawnState);
    // Security/shape/catalog/biome checks MUST still fail eagerly while distant.
    for (int mutation = 0; mutation < 6; ++mutation) {
        bad = bytes;
        if (mutation == 0) bad.back() ^= 1;
        if (mutation == 1) u32(bad, 288, 0xffffffffu);
        if (mutation == 2) u32(bad, 284, OriginalWorld::ChunkCells);
        if (mutation == 3) u32(bad, 20, 0);
        if (mutation == 4) bad[28] = 23;
        if (mutation == 5) bad[28] = uint8_t((bad[28] + 1) % 23);
        if (mutation) u32(bad, bad.size() - 4, hash(bad, bad.size() - 4));
        write(file, bad);
        rejects([&] { World invalid; invalid.beginStreaming(1); invalid.open(fixture.path(), root.path); });
    }
    write(file, bytes); fs::rename(file, file + ".good"); fs::create_symlink(file + ".good", file);
    rejects([&] { World invalid; invalid.open(fixture.path(), root.path); });
    fs::remove(file); fs::create_hard_link(file + ".good", file);
    rejects([&] { World invalid; invalid.open(fixture.path(), root.path); });
    fs::remove(file); fs::rename(file + ".good", file);
    fs::resize_file(file, 288 + size_t(OriginalWorld::ChunkCells) * 8 + 1);
    rejects([&] { World invalid; invalid.open(fixture.path(), root.path); });
    write(file, bytes);
    std::cout << "deferred-open plain ms=" << elapsed << " generated=1 distantModified=1\n";
}
void deferredLoadErrorStaysQueued() {
    OriginalWorld baseline(12345, 864, false, false), world(12345, 864, false, false);
    assert(baseline.setBlock(80, 1, 80, 0));
    const int key = 5 * 54 + 5;
    auto changes = baseline.modifiedChunks().at(key);
    // A redundant v2 edit is not silently discarded on first stream load.
    changes.edits.clear(); changes.edits[uint32_t(local(0, 200, 0))] = 0;
    world.restoreChunk(key, changes, true);
    world.beginStreaming(1); world.setStreamingCenter(80.5f, 80.5f);
    for (int i = 0; i < 2; ++i) {
        rejects([&] { world.processChunkLoads(1); });
        const auto info = world.streamingInfo();
        assert(info.deferred == 1 && info.queued == 9 && info.resident == 0 && info.totalGenerated == 0);
        assert(!world.chunkLoaded(80, 80) && world.blockAt(80, 200, 80) == 0);
    }
}
void asyncDeferredFailureKeepsWorldUsable() {
    Temp root;
    World fixture; fixture.create(12345, root.path, true, 864, false, false);
    const int cx = int(fixture.playerX()) / 16, cz = int(fixture.playerZ()) / 16;
    // A target neighbor is not spawn safety. Valid shape/biomes but redundant
    // high-air removal must fail at main-thread commit, not distant open/save.
    const auto bytes = overlay(fixture, cx + 1, cz, {{uint32_t(local(8, 200, 8)), 0}});
    const auto file = chunkFile(fixture, cx + 1, cz); write(file, bytes);
    World world; world.beginStreaming(1); world.open(fixture.path(), root.path);
    assert(world.streamingInfo().deferred == 1);
    world.setStreamingView(90, 0, 1);
    const auto before = world.streamingInfo();
    mcworld::ChunkPump pump;
    assert(pump.start(world, 9));
    world.update(.01f, 0, 0, false, 0); world.renderVolume(); world.biomeInfo(); world.save();
    assert(read(file) == bytes);
    while (pump.poll() == mcworld::ChunkPump::State::Running) std::this_thread::yield();
    assert(pump.poll() == mcworld::ChunkPump::State::Failed && !pump.error().empty());
    rejects([&] { pump.finish(); });
    const auto failed = world.streamingInfo();
    assert(failed.deferred == 1 && failed.queued > 0 && failed.loaded >= before.loaded);
    assert(!world.chunkLoaded((cx + 1) * 16, cz * 16));
    // Failure is not a global World lease. Render, movement, edits and saving
    // continue, including repeated stable failure inspection and explicit reset.
    world.renderVolume(); world.statusJson(); world.update(.01f, 0, 0, false, 0);
    assert(world.mine(0, -90)); world.save();
    assert(read(file) == bytes);
    pump.reset(); assert(pump.poll() == mcworld::ChunkPump::State::Idle && pump.error().empty());
}
void cancellationAndSettings() {
    Temp root;
    World world; world.beginStreaming(1); world.create(7, root.path, true, 864, false, false);
    const auto path = world.path(); const auto count = std::distance(fs::directory_iterator(root.path), fs::directory_iterator());
    for (const std::string stage : {"spawn", "initialsave", "post"}) for (int done : {0, 1}) {
        rejects([&] {
            world.create(12345, root.path, true, 864, false, false, [&](const auto& p) {
                if (p.stage == "post") assert(world.path() != path && fs::exists(world.path() + "/state.bin"));
                if (p.stage == stage && p.done == done) throw std::runtime_error("cancel");
            });
        });
        assert(world.path() == path && fs::exists(path + "/state.bin"));
        assert(std::distance(fs::directory_iterator(root.path), fs::directory_iterator()) == count);
    }
    World sized; sized.setRenderRadius(2); sized.create(7, root.path, true, 864, false, false);
    assert(!sized.streaming() && sized.renderVolume().width == 80);
    World legacyDefault; legacyDefault.create(7, root.path, true, 864, false, false);
    assert(!legacyDefault.streaming() && legacyDefault.renderVolume().width == 96);
}
int main() {
    rawStreaming(); worldStreamingAndScene(); physicsUnknownBoundary(); deferredSavedChunks();
    deferredLoadErrorStaysQueued(); asyncDeferredFailureKeepsWorldUsable(); cancellationAndSettings();
    std::cout << "chunk-streaming-test: budgets/priority/cancel/eviction/radii/unknown physics/Scene/deferred validation/progress/legacy passed\n";
}
