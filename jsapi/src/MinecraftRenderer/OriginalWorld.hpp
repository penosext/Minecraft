#pragma once

#include "World.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

namespace mcworld {
// Exclusively owned, single-thread-at-a-time bridge (ownership may transfer):
// BASE -> structures -> decoration subset -> edits. No internal threads.
// Not full BiomeDecorator/postProcess equivalence, caves or full-game physics.
class OriginalWorld {
public:
    static constexpr int Height = 256, DefaultSize = 5120;
    static constexpr int ChunkWidth = 16, ChunkCells = 16 * 256 * 16;
    static constexpr size_t CacheLimit = 64; // minimum; large ranges use target + dependency/safety slots
    static constexpr int MaxRadius = 12;
    // Settings accept up to MaxRadius, but the window, FINAL cache and lighting
    // all scale with the radius: radius 12 needs roughly 310 MiB, far beyond
    // what the device has free. This reports the largest radius that actually
    // fits in available memory, so a high setting degrades instead of being
    // killed by the allocator. Never returns less than 1.
    static int affordableRadius(int requested);
    using Edits = std::map<uint32_t, uint16_t>; // local y-z-x -> CURRENT catalog id+1
    struct ModifiedChunk {
        std::array<uint8_t, 256> biomes {};
        Edits edits;
        bool dirty = false;
        uint32_t persistedHash = 0;
        bool persisted = false;
        uint32_t formatVersion = 2; // v1 is removal-only; v2 uses saved-key palette IDs
        mutable bool baselineValidated = false;
    };

    // Low-level callers retain the pre-decoration default; World::create()
    // explicitly passes its public decoration=true default for new worlds.
    explicit OriginalWorld(uint32_t seed, int worldSize = DefaultSize, bool structures = true, bool decoration = false);
    int size() const;
    bool structuresEnabled() const;
    bool decorationsEnabled() const;
    struct BiomeMeta { int id; const char* name; const char* sourceName; uint32_t grassColor, foliageColor; };
    // ID 23 is unregistered upstream; unsupported/unregistered IDs throw.
    static const BiomeMeta& biomeMeta(int id);
    struct Landmark {
        int64_t id;
        std::string type;
        int x, z, biome, minX, minZ, maxX, maxZ, pieceCount;
    };
    std::vector<Landmark> landmarks(int logicalX, int logicalZ, int radiusBlocks = 256) const;
    static bool validSize(int worldSize);
    ~OriginalWorld();
    OriginalWorld(const OriginalWorld&) = delete;
    OriginalWorld& operator=(const OriginalWorld&) = delete;
    uint16_t blockAt(int x, int y, int z) const;
    uint8_t biomeAt(int x, int z) const;
    bool setBlock(int x, int y, int z, uint16_t value);
    struct BlockChange { int x, y, z; uint16_t value; }; // CURRENT catalog id+1
    // All validation/allocation precedes publication. Duplicate cells are invalid;
    // false (unloaded/no change) and exceptions never publish a partial batch.
    bool setBlocks(const std::vector<BlockChange>& changes);
    // Live state belongs to the owner/main thread. Async generation uses only
    // request values and an exclusively owned context, never this live instance.
    // Legacy single-radius APIs set display == preload. Neither range setter
    // generates chunks; independent ranges must satisfy 1 <= display <= preload <= 12.
    void setRenderRadius(int chunks);
    void setChunkRanges(int displayRadius, int preloadRadius);
    void beginStreaming(int radius);
    void endStreaming();
    bool streaming() const;
    void setStreamingCenter(float playerX, float playerZ);
    void setStreamingView(float yaw, float pitch, float aspect);
    std::vector<ChunkRequest> chunkRequests(int budget) const;
    bool commitChunk(GeneratedChunk&& chunk);
    // Only context configuration/dependency caches are reused. Live caches and
    // modified overlays are never copied; the returned baseline has no edits.
    static GeneratedChunk generateChunk(const ChunkRequest& request,
                                        std::unique_ptr<OriginalWorld>& isolatedContext);
    int processChunkLoads(int maxChunks = 1);
    bool chunkLoaded(int worldX, int worldZ) const;
    // Explicit spawn/physics loading only. Reads and edits never use this path.
    bool loadChunk(int worldX, int worldZ);
    StreamingInfo streamingInfo() const;
    uint64_t streamingRevision() const;
    // Updated in place for edits; rebuilt on center/radius/ready changes.
    // Large display ranges retain all nonair/edited levels and player headroom,
    // with originY=0 and height in [128,256]; logical build height stays 256.
    const RenderVolume& renderVolume(float playerX, float playerZ, float playerY = 0) const;
    size_t cachedChunkCount() const;
    int windowBlockCount(float playerX, float playerZ, float playerY = 0) const;
    const std::map<int, ModifiedChunk>& modifiedChunks() const;
    // Always validates biome/index/catalog/schema without terrain generation.
    // deferBaseline delays immutable baseline checks until first actual FINAL
    // load; invalid overlays throw before publication, never silently disappear.
    // v1 accepts only breakable removals; v2 never bedrock/redundant edits.
    // Saved IDs must already have been remapped by key.
    void restoreChunk(int key, ModifiedChunk chunk, bool deferBaseline = false);
    void markPersisted(int key, uint32_t checksum);
    // Material/subtype and supported metadata are resolved by catalog key.
    // Missing tiles/variants throw; never substitute stone. Orientation-only
    // metadata uses the catalog's representative until matching keys exist.
    static uint16_t catalogForTile(uint8_t tile, uint8_t data = 0);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace mcworld
