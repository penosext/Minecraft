#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mcworld { class World; struct RenderVolume; }
namespace legacy { struct BlockSpec; struct Vertex; }

namespace worldrender {
struct Point { float x, y, z; };
struct Box { Point low, high; };
struct Surface {
    Point origin, edgeU, edgeV, normal, dualU, dualV;
    float u, v, uu, uv, vu, vv;
    int face;
};
// Catalog-independent geometry also lets host tests exercise raw metadata before
// generated variants exist. Build once per BlockSpec, never inside the ray loop.
struct Geometry {
    bool fullCube = false, door = false;
    int data = 0;
    std::vector<Box> boxes;
    std::vector<Surface> planes;
};
struct Hit { float distance, u, v; int face; };
struct BiomeTint { float grassR, grassG, grassB, leafR, leafG, leafB; };
// Original gluPerspective uses a VERTICAL 70-degree base FOV. All world-space
// compositors must use focalLength; the hand has its own source projection.
float verticalFov();
float focalLength(int width, int height);
// Upper bound for LightingSettings::maxDistance. Streaming reports
// radius*16-8, so 12 chunks => 184. The old hard cap of 40 meant ranges of 4+
// loaded more terrain without letting the player see any further.
constexpr float MaxViewDistance = 184.0f;
struct LightingSettings {
    bool enabled = true;
    bool ambientOcclusion = true;
    // Explicit frozen-reference opt-out, NEVER selected by production defaults.
    bool legacyProjection = false;
    int moonPhase = 0;
    float maxDistance = 40.0f;
    uint64_t revision = 0; // Raw-volume callers MUST increment after in-place edits.
    std::string_view cachePath;
};
struct TileLighting { uint8_t lightBlock, emission; bool occludesAO; };
TileLighting tileLighting(uint16_t catalogId);
struct LightSample { uint8_t sky, block; };
// Separate bytes, not nibbles and not part of physics/voxel storage. A cache owns
// exactly two volume-sized byte arrays; scratch propagation queues are transient.
// A terrain-revision change rebuilds INCREMENTALLY into spare buffers across
// several update() calls while rendering keeps sampling the last completed
// result, so one chunk commit cannot stall a frame. Lighting therefore lags
// terrain by a few frames: a deliberate latency/stutter trade, NOT the original
// asynchronous Level lighting engine. Geometry changes (window move/resize) and
// the very first build cannot lag and are completed immediately.
struct LightingBuffer {
    std::vector<uint8_t> sky, block;
    std::string path;
    const uint16_t* source = nullptr;
    int originX = 0, originY = 0, originZ = 0, width = 0, height = 0, depth = 0;
    uint64_t revision = 0, builds = 0;
    // Columns seeded per update() while a revision rebuild is in flight. The
    // propagation that follows is bounded by the seeds those columns produced.
    // 256 columns/frame measured ~5.9 ms worst case on a 112x256x112 window,
    // versus ~156 ms for the equivalent single-shot rebuild. Larger budgets
    // converge in fewer frames but reintroduce visible stalls.
    static constexpr int DefaultColumnBudget = 256;
    std::vector<uint8_t> pendingSky, pendingBlock;
    const uint16_t* pendingSource = nullptr;
    uint64_t pendingRevision = 0;
    int pendingColumn = -1; // >= 0 while an incremental rebuild is in flight
    // Propagation is a flood fill, so its frontier is carried across calls too;
    // seeding alone is not what makes a rebuild expensive.
    enum class Stage { Seed, SeedSky, SpreadSky, SeedBlock, SpreadBlock };
    Stage pendingStage = Stage::Seed;
    std::vector<int> pendingQueue;
    size_t pendingFront = 0;
    int pendingScan = 0;
    uint64_t incrementalBuilds = 0, incrementalSteps = 0;
    // Column visit order for the incremental seed stage. Columns the player is
    // looking at are rebuilt first so the view catches up before the far side.
    std::vector<int> pendingOrder;
    uint64_t localRefreshes = 0, patchedEditRevision = 0, patchedViewRevision = 0;
    int patchedViewYaw = -999, patchedViewPitch = -999;
    const uint8_t* loadedColumns = nullptr;
    // Camera used to order the next rebuild. Set by the renderer each frame.
    float viewEyeX = 0, viewEyeZ = 0, viewDirX = 0, viewDirZ = 1;
    bool viewValid = false;
    // Immediately relight a small box around an edit (mining/placing) so the
    // change is visible on the very next frame instead of waiting for the
    // incremental pass. Sky columns inside the box are reseeded from the top and
    // the affected neighbourhood is re-propagated; this is a bounded local fix,
    // not a full-volume rebuild, so distant indirect light can still lag.
    bool refreshAround(const mcworld::RenderVolume& volume, int x, int y, int z, int radius = 16);
    bool update(const mcworld::RenderVolume& volume, std::string_view cachePath, uint64_t terrainRevision,
                int columnBudget = DefaultColumnBudget);
    LightSample sample(int x, int y, int z) const;
};
// Test/diagnostic view of the current thread's rendering cache.
const LightingBuffer& lightingBuffer();
// Straight original ARGB texel from the rotated source quad, zero outside it.
// Direction is world-space; phase is ALREADY Dimension::getTimeOfDay, not ticks.
uint32_t celestialTexel(Point direction, float phase, int moonPhase = 0);
Geometry buildGeometry(const legacy::BlockSpec& spec);
bool rayIntersection(const Geometry& geometry, Point origin, Point direction,
                     float nearDistance, float farDistance, Hit& hit);
// Two-sided quads, retaining the source mesh UVs. Door data is composite when
// supplied: low bits from lower half, bit 16 = upper-half right hinge. An
// isolated upper preview cannot infer direction: its explicit fallback is 0.
std::vector<legacy::Vertex> buildShapeMesh(const legacy::BlockSpec& spec, int compositeDoorData = -1);
// Raw volume entry point uses exactly the production DDA, including absolute
// window bounds. The caller owns a width*height*depth y-z-x array of catalog ids.
void renderVolumeFrame(uint32_t* pixels, int width, int height,
                       const mcworld::RenderVolume& volume, Point eye,
                       float yaw, float pitch, int pixelStep = 3,
                       float daylight = 1.0f, float timeOfDay = -1.0f,
                       const BiomeTint* tints = nullptr,
                       const LightingSettings& lighting = {});
} // namespace worldrender

// pixelStep is the side of each ray's output tile: 1 = full resolution,
// 2 = original quality, 3 = default. Nonpositive steps are treated as 1.
// Null output, nonpositive dimensions, unloaded world or nonfinite angles are
// ignored. Output must hold width * height ARGB pixels, including clipped tiles.
void renderWorldFrame(uint32_t* pixels, int width, int height,
                      const mcworld::World& world, float yaw, float pitch, int pixelStep = 3,
                      float daylight = 1.0f, float timeOfDay = -1.0f,
                      const worldrender::LightingSettings& lighting = {});
