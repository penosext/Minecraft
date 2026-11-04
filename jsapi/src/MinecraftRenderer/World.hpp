#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mcscene { class Scene; }

// CPU live state belongs to its owning thread. Background workers exchange only
// copied requests and generated results; they never read or mutate a live World.
// New worlds use extracted original biome/density/surface
// generation; custom-v1 saves retain their original terrain, dimensions and I/O.
namespace mcworld {
// Legacy dimensions ONLY. Use World's runtime accessors for new code.
constexpr int Width = 64;
constexpr int Height = 48;
constexpr int Depth = 64;
struct RenderVolume {
    const uint16_t* data;
    int originX, originY, originZ, width, height, depth;
    // One byte per window column (width*depth, x-major). Zero means that
    // column's chunk is NOT resident, so its cells are padding rather than real
    // air; lighting treats such columns as unlit instead of letting skylight
    // pour through them. Null for non-streaming volumes, where every cell exists.
    const uint8_t* loadedColumns = nullptr;
};
class OriginalWorld;

struct BlockHit {
    bool hit = false;
    // [FIX] True when the ray left the resident streaming window (or crossed an
    // unloaded column) before reaching `reach`. This is "unknown", NOT
    // "nothing there": survival mining keeps its target alive and the crack
    // overlay must not vanish. Callers that only care about a real block
    // check `hit && !unknown`.
    bool unknown = false;
    int x = 0, y = 0, z = 0, face = -1;
    uint16_t block = 0; // current catalog index + 1, not original TileID
    float distance = 0;
};

struct CreateProgress {
    std::string stage; // "spawn", "initialsave", "post"; phase-local completed work
    int done = 0, total = 0;
};
using ProgressCallback = std::function<void(const CreateProgress&)>;
struct StreamingInfo {
    bool enabled = false;
    int radius = 0, windowSize = 96, maxDistance = 40; // radius is the display radius
    int preloadRadius = 0, displayTarget = 0, displayLoaded = 0;
    int queued = 0, loaded = 0, target = 0, resident = 0; // preload range
    int centerChunkX = -1, centerChunkZ = -1;
    uint64_t totalGenerated = 0; // successful FINAL chunks, including regeneration
    int baseCached = 0, structureCached = 0, deferred = 0;
    size_t cacheLimit = 64; // FINAL target + bounded dependency/safety slots
    uint64_t cacheBytes = 0, windowBytes = 0; // vector capacities, excluding generator plans/renderer
    double lastLoadMs = 0; // cooperative step elapsed time, NOT a time budget
};

struct ChunkRequest {
    uint64_t epoch = 0;
    uint32_t seed = 0;
    int worldSize = 0, key = -1;
    bool structures = false, decoration = false, carvers = false;
};
struct GeneratedChunk {
    ChunkRequest request;
    std::vector<uint16_t> baseline;
    std::array<uint8_t, 256> biomes {};
    double loadMs = 0;
};

class World {
public:
    using CreateProgress = mcworld::CreateProgress;
    using ProgressCallback = mcworld::ProgressCallback;
    using StreamingInfo = mcworld::StreamingInfo;
    bool loaded() const { return loaded_; }
    // Only create() makes directories; existing world directories are never reused.
    // The root override is intended for host tests. Production uses /tmp/mc.
    // Flags apply only to original generation. Decoration creates generator v4
    // (v3 + original caves/canyons + ores); disabling it selects v1 (no
    // structures) or v2 (structures). Existing saves ALWAYS retain their
    // generation baseline; open() never upgrades it (v3 stays cave-free).
    // Callback runs on the calling/owning thread; throwing cancels transactionally.
    // create() loads only spawn/safety chunks, never the whole render target.
    void create(uint32_t seed, const std::string& root = "/tmp/mc", bool original = true, int worldSize = 5120, bool structures = true, bool decoration = true, ProgressCallback progress = {});
    // Explicit generation selection, e.g. carvers=false for a v3 baseline.
    // carvers requires decoration (v4 is v3 + carvers + ores).
    void createGeneration(uint32_t seed, const std::string& root, bool original, int worldSize, bool structures,
                          bool decoration, bool carvers, ProgressCallback progress = {});

    // Runtime-only settings survive create/open on this object, not save files.
    // Radius 1..12 -> 48..400, minimum safe ray distance radius*16-8. The default
    // remains legacy 96. The legacy single-radius APIs set display == preload.
    // Range setters do NOT enable streaming or synchronously generate terrain.
    void setRenderRadius(int chunks);
    // Atomically retarget runtime ranges; require 1 <= display <= preload <= 12.
    void setChunkRanges(int displayRadius, int preloadRadius);
    void beginStreaming(int radius);
    void endStreaming();
    bool streaming() const { return streaming_; }
    // Explicit synchronous loading for creation/open jobs and compatibility.
    // Runtime uses copied requests, isolated generation and main-thread commit.
    int processChunkLoads(int maxChunks = 1);
    // Main-thread request/commit boundary; background generation never borrows World.
    std::vector<ChunkRequest> chunkRequests(int maxChunks = 1) const;
    bool commitChunk(GeneratedChunk&& chunk);
    void setStreamingView(float yawDegrees, float pitchDegrees, float aspect);
    // Outside=false; legacy generation reports all in-world chunks accessible.
    bool chunkLoaded(int worldX, int worldZ) const;
    StreamingInfo streamingInfo() const;
    std::string streamingInfoJson() const;
    void open(const std::string& path, const std::string& root = "/tmp/mc");

    // --- Multi-dimension save container (format only; no Nether terrain) ---
    // Save-format generations for the original-terrain format:
    //   v1 = the shipped single-dimension layout. 14 metadata fields, or 15 with
    //        decoration. Chunk overlays live flat in the world directory as
    //        chunk-<key>.bin and state.bin is exactly 44 bytes.
    //   v2 = adds a "dimensions" manifest and a "saveVersion" field, stores the
    //        active dimension in state.bin, and reserves per-dimension chunk
    //        subdirectories for non-overworld slots.
    // v1 saves are opened read-compatibly and are NEVER rewritten as v2: open()
    // records the generation it read and save() reproduces the same one, so an
    // untouched v1 directory stays byte-identical. upgradeToV2() is the only
    // transition and it is explicit.
    enum class SaveVersion { V1 = 1, V2 = 2 };
    SaveVersion saveVersion() const { return saveVersion_; }

    // level.json is byte-identical in v1 and v2 and is never rewritten, not
    // even by the upgrade, so hash(level.json) is stable across it. Companion
    // stores that key their identity to that hash (Inventory does) need no
    // changes and no awareness of the container generation.

    // Stable dimension slot ids. These are UNSIGNED storage slots, not upstream
    // signed dimension numbers: the save metadata parser accepts unsigned
    // integers only. Slot 1 maps to the upstream "DIM-1" directory name, which
    // is the one on-disk convention this format deliberately matches.
    static constexpr int OverworldDimension = 0;
    static constexpr int NetherDimension = 1;
    static constexpr int MaxDimensionSlot = 15; // bounds a hostile manifest

    // Chunk overlay subdirectory for a slot, relative to the world directory.
    // The overworld returns "" and therefore KEEPS the flat chunk-<key>.bin
    // layout in both v1 and v2. That is deliberate: moving overworld overlays
    // into a subdirectory would rewrite every existing save's terrain files on
    // first open, which is exactly the implicit mutation requirement 1 forbids.
    // A v2 overworld directory stays readable by the v1 enumerator, so the only
    // new on-disk state is additive.
    static std::string dimensionDirectory(int slot);

    // Dimension slots present in this save, ascending, always including 0.
    const std::vector<int>& dimensions() const { return dimensions_; }
    bool hasDimension(int slot) const;
    // Active slot persisted in state.bin. v1 saves always report the overworld.
    int currentDimension() const { return currentDimension_; }

    // Promote a loaded v1 save in place to the v2 container, then persist it.
    // Terrain, player state, seed, palette and generator baseline are unchanged;
    // this only rewrites level.json and state.bin. Already-v2 saves are a no-op.
    // The overworld's existing flat chunk overlays are NOT moved or rewritten.
    void upgradeToV2();
    // Register an additional dimension slot in a v2 save and persist the
    // manifest. This reserves storage ONLY: no terrain is generated and the
    // player is not moved. Requires v2; throws on v1 or an invalid slot.
    void addDimension(int slot);
    // Record which dimension the player is in. Storage bookkeeping only; it does
    // NOT load, generate or swap any terrain, and the slot must already be
    // registered. Travel belongs to the later Nether work.
    void setCurrentDimension(int slot);
    // Throws std::runtime_error on failed writes, flushes, closes, or renames.
    // Original custom-v1 chunks/metadata are immutable. Saves atomically replace
    // edits.bin when terrain changes, then state.bin on every save. Each file is
    // atomic; a failed state write may leave committed removals with old player
    // state, and save() can be retried. Untouched v1 saves need no edits.bin.
    // Call save() on app pause/exit in addition to the 10s autosave.
    void save();

    // Creative-style instant removal, NOT survival hardness/drops or original
    // game physics. Eye = feet + 1.62; yaw 0 = +Z, 90 = +X, positive pitch = up.
    // Cube-cell DDA (not exact renderer shapes). Bedrock occludes; liquids and
    // catalog air are skipped. Max/default 5.0 = upstream creative getPickRange;
    // multiplayer noncreative is 4.5, local SurvivalMode is 4.0. This API is
    // creative-style, not survival hardness/drops. Invalid float inputs throw.
    // Face: 0 down, 1 up, 2 -Z, 3 +Z, 4 -X, 5 +X; -1 if eye starts inside.
    BlockHit pick(float yawDegrees, float pitchDegrees, float reach = 5.0f) const;
    bool mine(float yawDegrees, float pitchDegrees, float reach = 5.0f);
    // Current zero-based catalog index from the last successful mine, else -1.
    int lastMinedBlock() const { return lastMinedBlock_; }
    // Most recent terrain edit (mine/place/interact) in absolute cell coords, so
    // a renderer can relight just that neighbourhood immediately. revision 0
    // means nothing has been edited yet this session.
    struct EditMark { int x = 0, y = 0, z = 0; uint64_t revision = 0; };
    EditMark lastEdit() const { return lastEdit_; }
    // Adjacent face only, in bounds; only air/liquid may be replaced. Rejects
    // catalog air/liquid selections and any player-AABB overlap. Custom-v1
    // placement throws unsupported; its immutable removal-only format is kept.
    bool place(float yawDegrees, float pitchDegrees, int catalogIndex, float reach = 5.0f);
    // Source item ID + material aux, NOT a render/world-state selection. Audited
    // placement subset derives metadata from face/yaw/click height. Unsupported
    // rules/states return false; two-cell doors publish atomically. Custom-v1
    // remains unsupported (throws), exactly as the legacy place() API above.
    bool placeItem(float yawDegrees, float pitchDegrees, int sourceItemId, int aux, float reach = 5.0f);
    // Source Tile::use subset: right-click interaction, which upstream runs
    // BEFORE placement and which suppresses placement when it returns true
    // (MultiPlayerGameMode::useItemOn). Returns true when the click was handled
    // at all, including the iron door's source-faithful "consumed, unchanged"
    // case, so a caller can gate its own place/placeItem call on the result.
    // Only wooden/iron doors are in scope this round; lever, button and
    // repeater have no powered render state in the embedded catalog, and
    // container/GUI tiles have no container model, so all of those return
    // false. Unlike placement there is NO player-AABB rejection: upstream lets
    // you open a door you are standing in. Unloaded chunks, out-of-world cells
    // and unpaired door halves fail safely. Custom-v1 saves throw unsupported,
    // exactly as place()/placeItem() do.
    bool useBlock(float yaw, float pitch, float reach = 5.0f);
    bool till(float yaw, float pitch, float reach = 5.0f);
    bool grounded() const;
    bool underWater() const;
    // Terrain revision changes on create/open/edits and streaming ready/center
    // changes, not legacy movement, reads, or save.
    uint64_t revision() const;
    int width() const;
    int height() const;
    int depth() const;
    bool originalGeneration() const;
    // y-z-x order, current catalog id+1. Legacy: full volume, origin=0.
    // Original: a 96x256x96 window by default, or radius-selected dynamic window
    // with ABSOLUTE origin. Streaming copies resident chunks only (unknown=air,
    // including lighting boundaries: transient cave light leaks are possible).
    // Radius windows are chunk-aligned and clamped inside [0,size). Rebuilt
    // on player chunk changes, NOT every frame. A renderer must include origin
    // in its cache key in addition to revision(). Edits update it in place.
    // References/pointers expire on rebuild, create/open, or destruction.
    const RenderVolume& renderVolume() const;
    // Compatibility only: original mode returns renderVolume().data, NOT the
    // whole world's dense array. Never index it with width()/depth(); use the
    // RenderVolume dimensions and subtract its origin instead.
    const uint16_t* blockData() const;

    // 0 = air, otherwise the CURRENT embedded catalog index + 1. Outside is air
    // for rendering; physics independently treats all six world faces as barriers.
    uint16_t blockAt(int x, int y, int z) const;
    // Absolute logical cells [0,width) x [0,depth), not centered source coords.
    // Legacy custom worlds have no recorded biome: returns -1, never Plains.
    // Outside the finite world throws, consistently for both generation modes.
    int biomeAt(int logicalX, int logicalZ) const;
    // JSON object for the player's current biome: id, Chinese name, sourceName,
    // source, known and exact upstream grass/foliage RGB (legacy common colors).
    std::string biomeInfo() const;
    uint32_t grassColorAt(int logicalX, int logicalZ) const;
    uint32_t foliageColorAt(int logicalX, int logicalZ) const;
    bool structuresEnabled() const;
    bool decorationsEnabled() const;
    bool carversEnabled() const; // v4 caves/canyons/ores
    // Deterministic structure locations, in logical coordinates; no teleport.
    std::string landmarks() const;
    // Source-audited block AABBs with simplified seconds-based gravity, sliding,
    // step and ladder movement. No full fluid dynamics/damage or Entity engine.
    // Feet coordinates, radius .25, height 1.8. Ground jump is rising-edge triggered.
    // dt is capped at .1s and subdivided; invalid/nonfinite inputs throw.
    // yaw 0 = +Z, yaw 90 = +X; positive strafe is right.
    void update(float dt, float forward, float strafe, bool jump, float yawDegrees);
    // Absolute feet-position move for the /tp command. Rejects nonfinite or
    // out-of-world coordinates, unloaded chunks, and any target whose body
    // overlaps terrain, so a teleport can never bury the player in a block.
    // Clears vertical velocity and re-resolves grounded state. Never generates.
    bool teleport(float x, float y, float z);

    const std::string& path() const;
    uint32_t seed() const;
    float playerX() const;
    float playerY() const;
    float playerZ() const;
    // Original mode counts nonair cells in the render window, NOT the entire
    // lazy world (counting the latter would force all 2916 chunks to generate).
    int blockCount() const;
    std::string statusJson() const;

private:
    friend class mcscene::Scene;
    void requireLoaded() const;
    BlockHit pickImpl(float yawDegrees, float pitchDegrees, float reach, bool includeWater) const;
    void generate();
    void spawnOriginal(const ProgressCallback& progress);
    void configureOriginal();
    void loadPlayerSafety();
    void saveOriginal();
    // Serializes level.json for the CURRENT saveVersion_; v1 output is
    // byte-identical to the shipped single-dimension format.
    std::vector<unsigned char> encodeOriginalMetadata() const;
    bool solid(int x, int y, int z) const;
    bool collides(float x, float y, float z) const;
    bool onLadder() const;
    void moveVertical(float distance);
    void moveHorizontal(float dx, float dz);

    bool streaming_ = false;
    int renderRadius_ = 0, preloadRadius_ = 0; // 0 preserves the original 96 window
    bool loaded_ = false;
    bool persisted_ = false;
    bool grounded_ = false;
    bool jumpHeld_ = false;
    bool dirty_ = false;
    bool terrainDirty_ = false;
    std::string root_, path_;
    uint64_t rootDevice_ = 0, rootInode_ = 0, worldDevice_ = 0, worldInode_ = 0;
    uint32_t seed_ = 0, chunkHash_ = 0, paletteHash_ = 0;
    mutable uint64_t revision_ = 0, observedStreamingRevision_ = 0;
    std::shared_ptr<OriginalWorld> original_;
    mutable RenderVolume renderVolume_ {nullptr, 0, 0, 0, Width, Height, Depth};
    std::vector<std::string> originalPalette_; // Immutable saved-key order.
    uint32_t originalMetadataHash_ = 0;
    // Generation actually read from disk; save() never silently raises it.
    SaveVersion saveVersion_ = SaveVersion::V1;
    std::vector<int> dimensions_ {OverworldDimension}; // ascending, unique, always holds 0
    int currentDimension_ = OverworldDimension;
    std::vector<uint16_t> blocks_;
    std::vector<uint32_t> edits_; // Sorted unique local cell indices, all -> air.
    std::vector<uint32_t> persistedEdits_; // Detect removed/replaced/corrupt overlays.
    int blockCount_ = 0;
    int lastMinedBlock_ = -1;
    EditMark lastEdit_ {};
    float x_ = 0, y_ = 0, z_ = 0, velocityY_ = 0, saveTime_ = 0;
};
} // namespace mcworld