// Native regression; registration/build belongs to the integration owner.
#ifdef NDEBUG
#undef NDEBUG
#endif
#define main preloadWorldHelpersMain
#include "world-test.cpp"
#undef main
#include "OriginalWorld.hpp"
#include "ChunkPump.hpp"

namespace {
using mcworld::OriginalWorld;
using mcworld::ChunkRequest;
constexpr int Chunks = 864 / 16;
constexpr float Center = 440.5f;

size_t position(const std::vector<ChunkRequest>& requests, int key) {
    for (size_t i = 0; i < requests.size(); ++i) if (requests[i].key == key) return i;
    assert(false); return requests.size();
}

void independentRangesAndHiddenCommits() {
    OriginalWorld world(12345, 864, false, false);
    world.beginStreaming(1); world.setStreamingCenter(Center, Center);
    world.setChunkRanges(1, 3);
    const auto initial = world.streamingInfo();
    assert(initial.radius == 1 && initial.preloadRadius == 3 && initial.windowSize == 48);
    assert(initial.target == 49 && initial.queued == 49 && initial.displayTarget == 9);
    assert(initial.loaded == 0 && initial.displayLoaded == 0 && initial.totalGenerated == 0);
    const auto volume = world.renderVolume(Center, Center);
    assert(volume.width == 48 && volume.depth == 48 && volume.loadedColumns);
    std::vector<uint16_t> displayed(volume.data, volume.data + size_t(48) * 256 * 48);
    std::vector<uint8_t> mask(volume.loadedColumns, volume.loadedColumns + 48 * 48);
    world.setStreamingView(0, 0, 1);
    const auto requests = world.chunkRequests(100);
    const int centerKey = 27 * Chunks + 27;
    assert(requests.size() == 49 && requests.front().key == centerKey);
    // All display chunks precede preload, with visible first inside each range.
    assert(position(requests, centerKey + Chunks) < position(requests, centerKey - Chunks));
    assert(position(requests, centerKey - Chunks) < position(requests, centerKey + 2 * Chunks));
    assert(position(requests, centerKey + 2 * Chunks) < position(requests, centerKey - 2 * Chunks));
    world.setStreamingView(180, 0, 1);
    const auto reversed = world.chunkRequests(100);
    assert(position(reversed, centerKey - Chunks) < position(reversed, centerKey + Chunks));
    assert(position(reversed, centerKey + Chunks) < position(reversed, centerKey - 2 * Chunks));
    assert(position(reversed, centerKey - 2 * Chunks) < position(reversed, centerKey + 2 * Chunks));
    assert(reversed.front().epoch == requests.front().epoch);
    assert(world.streamingInfo().totalGenerated == 0);

    std::unique_ptr<OriginalWorld> context;
    int visibleCount = 0, hiddenCount = 0;
    for (const auto& request : requests) {
        const int x = (request.key % Chunks) * 16, z = (request.key / Chunks) * 16;
        const bool visible = x >= volume.originX && x < volume.originX + volume.width &&
                             z >= volume.originZ && z < volume.originZ + volume.depth;
        const auto revision = world.streamingRevision();
        assert(world.commitChunk(OriginalWorld::generateChunk(request, context)));
        if (visible) {
            ++visibleCount;
            assert(world.streamingRevision() != revision);
            displayed.assign(volume.data, volume.data + displayed.size());
            mask.assign(volume.loadedColumns, volume.loadedColumns + mask.size());
        } else {
            ++hiddenCount;
            assert(world.streamingRevision() == revision);
            assert(std::equal(displayed.begin(), displayed.end(), volume.data));
            assert(std::equal(mask.begin(), mask.end(), volume.loadedColumns));
        }
        const auto info = world.streamingInfo();
        assert(info.displayLoaded == visibleCount && info.loaded == visibleCount + hiddenCount);
        assert(info.loaded + info.queued == 49 && info.resident == info.loaded);
        assert(std::count(volume.loadedColumns, volume.loadedColumns + 48 * 48, uint8_t(1)) == visibleCount * 256);
        assert(world.cachedChunkCount() <= OriginalWorld::CacheLimit);
        assert(world.renderVolume(Center, Center).data == volume.data);
    }
    assert(visibleCount == 9 && hiddenCount == 40);
    assert(world.streamingInfo().resident == 49 && world.streamingInfo().queued == 0);
    const auto generated = world.streamingInfo().totalGenerated;
    world.setChunkRanges(2, 3);
    assert(world.streamingInfo().target == 49 && world.streamingInfo().displayTarget == 25);
    assert(world.streamingInfo().displayLoaded == 25 && world.renderVolume(Center, Center).width == 80);
    world.setChunkRanges(1, 2);
    assert(world.streamingInfo().target == 25 && world.streamingInfo().resident == 25);
    assert(world.renderVolume(Center, Center).width == 48);
    assert(world.streamingInfo().totalGenerated == generated);
    world.setChunkRanges(1, 3);
    // Reads/view changes neither load the outer preload ring nor anything beyond it.
    for (float pitch : {-90.f, 0.f, 90.f}) {
        world.setStreamingView(90, pitch, 2.4f);
        world.renderVolume(Center, Center);
        assert(world.blockAt(31 * 16, 64, 27 * 16) == 0);
    }
    assert(world.streamingInfo().totalGenerated == generated && world.streamingInfo().queued == 24);
    const auto edge = world.renderVolume(.25f, 863.75f);
    assert(edge.originX == 0 && edge.originZ == 816 && edge.width == 48);
    assert(world.streamingInfo().displayTarget == 9 && world.streamingInfo().target == 49);
    assert(world.streamingInfo().totalGenerated == generated);
    for (const auto& request : world.chunkRequests(100)) {
        assert(request.key % Chunks >= 0 && request.key % Chunks < 7);
        assert(request.key / Chunks >= Chunks - 7 && request.key / Chunks < Chunks);
    }
}

void atomicSettingsAndEpochs() {
    OriginalWorld world(7, 864, false, false);
    world.beginStreaming(1); world.setStreamingCenter(Center, Center);
    auto old = world.chunkRequests(1).front();
    world.setChunkRanges(1, 3);
    assert(world.chunkRequests(1).front().epoch != old.epoch);
    mcworld::GeneratedChunk stale; stale.request = old; // rejected before payload validation
    assert(!world.commitChunk(std::move(stale)));
    old = world.chunkRequests(1).front();
    world.setChunkRanges(2, 3); // display-only change still obsoletes the previous batch
    assert(world.chunkRequests(1).front().epoch != old.epoch);
    stale.request = old; assert(!world.commitChunk(std::move(stale)));
    const auto current = world.chunkRequests(1).front();
    const auto revision = world.streamingRevision();
    world.setChunkRanges(2, 3);
    assert(world.streamingRevision() == revision && world.chunkRequests(1).front().epoch == current.epoch);
    for (const auto& pair : {std::pair<int, int>{0, 3}, {13, 3}, {1, 0}, {1, 13}, {3, 2}}) {
        rejects([&] { world.setChunkRanges(pair.first, pair.second); });
        const auto info = world.streamingInfo();
        assert(info.radius == 2 && info.preloadRadius == 3 && info.target == 49 && info.displayTarget == 25);
        assert(world.chunkRequests(1).front().epoch == current.epoch);
    }
    assert(world.streamingInfo().totalGenerated == 0);
    // Legacy APIs always restore equal ranges, including same-display setters.
    for (int radius : {1, 2, 3}) {
        world.setRenderRadius(radius);
        auto info = world.streamingInfo();
        assert(info.radius == radius && info.preloadRadius == radius);
        assert(info.target == (radius * 2 + 1) * (radius * 2 + 1) && info.displayTarget == info.target);
        // The window grows with residency, so it is bounded by the requested
        // radius rather than jumping straight to it: raising the radius must
        // never publish a wall of unloaded columns.
        const auto& grown = world.renderVolume(Center, Center);
        assert(grown.width <= (radius * 2 + 1) * 16 && grown.width >= 48);
        assert(grown.width == grown.depth && grown.loadedColumns);
        // With nothing resident the window stays at the legacy single-ring
        // minimum instead of expanding to the requested radius.
        assert(grown.width == 48);
        // The reported view distance must stay inside the published window, or
        // rays reach past loaded terrain and the unloaded boundary shows up as
        // a wall. This is the regression behind the reported world-edge effect.
        assert(world.streamingInfo().maxDistance <= grown.width / 2);
        assert(world.streamingInfo().windowSize == grown.width);
        world.setChunkRanges(1, 3); world.beginStreaming(radius);
        info = world.streamingInfo();
        assert(info.radius == radius && info.preloadRadius == radius && info.target == info.displayTarget);
    }
    assert(world.streamingInfo().totalGenerated == 0);
}

void runtimePersistenceAndPumpRetarget() {
    Temp root;
    World world; world.beginStreaming(1); world.setChunkRanges(1, 3);
    world.create(12345, root.path, true, 864, false, false);
    assert(world.streamingInfo().radius == 1 && world.streamingInfo().preloadRadius == 3);
    assert(world.streamingInfo().target == 49 && world.renderVolume().width == 48);
    const auto metadata = read(world.path() + "/level.json");
    const auto json = world.streamingInfoJson();
    assert(json.find("\"preloadRadius\":3") != std::string::npos);
    assert(json.find("\"displayTarget\":9") != std::string::npos);
    assert(json.find("\"displayLoaded\":") != std::string::npos);
    const auto generated = world.streamingInfo().totalGenerated;
    world.setChunkRanges(2, 3); world.renderVolume();
    assert(world.streamingInfo().totalGenerated == generated);
    world.save(); assert(read(world.path() + "/level.json") == metadata);
    const auto path = world.path();
    world.open(path, root.path);
    assert(world.streamingInfo().radius == 2 && world.streamingInfo().preloadRadius == 3);
    // Reopening only loads the player safety chunks, so the published window is
    // still the single-ring minimum and grows as the preload queue drains.
    assert(world.streamingInfo().target == 49 && world.renderVolume().width == 48);

    mcworld::ChunkPump pump;
    for (int change = 0; change < 2; ++change) {
        assert(pump.start(world, 4));
        // No poll/finish yet: every copied request must become obsolete, even
        // for keys retained in the new target or a display-only range change.
        world.setChunkRanges(1, change ? 1 : 3);
        world.renderVolume();
        const auto before = world.streamingInfo();
        const auto revision = world.revision();
        const auto result = pump.finish();
        assert(result.count == 0 && result.info.loaded == before.loaded);
        assert(world.streamingInfo().totalGenerated == before.totalGenerated);
        assert(world.streamingInfo().queued == before.queued && world.revision() == revision);
    }
    // Current-epoch work remains usable after both stale batches were discarded.
    assert(pump.start(world, 1)); assert(pump.finish().count == 1);
    world.setChunkRanges(1, 3);
    rejects([&] { world.setChunkRanges(3, 2); });
    assert(world.streamingInfo().radius == 1 && world.streamingInfo().preloadRadius == 3);
    // Preserve the runtime pair through both custom-v1 candidate-copy paths.
    world.create(7, root.path, false);
    const auto legacyPath = world.path(); world.open(legacyPath, root.path);
    world.create(12345, root.path, true, 864, false, false);
    assert(world.streamingInfo().radius == 1 && world.streamingInfo().preloadRadius == 3);
    assert(world.streamingInfo().target == 49 && world.renderVolume().width == 48);
    World unconfigured; unconfigured.open(path, root.path);
    assert(unconfigured.streamingInfo().radius == 0 && unconfigured.streamingInfo().preloadRadius == 0);
    assert(read(path + "/level.json") == metadata); // runtime-only, no palette/baseline changes
}
} // namespace

int main() {
    independentRangesAndHiddenCommits(); atomicSettingsAndEpochs(); runtimePersistenceAndPumpRetarget();
    std::cout << "preload-settings-test: independent ranges/hidden commits/masks/epochs/pump/runtime persistence/legacy passed\n";
}
