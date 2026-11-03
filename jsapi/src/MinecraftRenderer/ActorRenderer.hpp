#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Aspect is the FULL parent framebuffer ratio before the original split-screen
// multiplier. With FullViewport, 0 selects w/h. With a split, pass the parent
// ratio explicitly (or 0 derives it from the child viewport's w/h).
enum class FirstPersonViewport { FullViewport, SplitHorizontal, SplitVertical };
struct FirstPersonState {
    float aspect = 0.0f;
    float fov = 70.0f; // vertical getFov(a,false), NOT world sprint/options FOV
    float swing = 0.0f; // getAttackAnim(a), already interpolated; [0,1]
    float equip = 1.0f; // oHeight+(height-oHeight)*a; 1 = fully equipped
    FirstPersonViewport viewport = FirstPersonViewport::FullViewport;
    bool bob = false;
    float bobPhase = 0.0f; // original b=-(walkDist+(walkDist-walkDistO)*a), NOT radians
    float bobAmount = 0.0f; // oBob+(bob-oBob)*a
    float bobTilt = 0.0f; // oTilt+(tilt-oTilt)*a, degrees
    float pitchLag = 0.0f; // interpolated pitch-xBob, degrees (source multiplies .1)
    float yawLag = 0.0f; // interpolated yaw-yBob, degrees
    float farPlane = 512.0f; // renderDistance*2; default viewDistance=0
    int heldBlockIndex = -1; // inventory catalog index; -1 empty hand, not an item id
};

// Native diagnostics use the exact same matrices/vertices as rasterization.
// Matrices are row-major, applied to COLUMN vectors (OpenGL postmultiply order).
struct FirstPersonVertex { float x = 0, y = 0, z = 0; };
struct FirstPersonProjectedVertex { float x = 0, y = 0, depth = 0; bool inFrustum = false; };
struct FirstPersonUV { float u = 0, v = 0; }; // source texel coordinates, not normalized
struct FirstPersonDiagnostics {
    bool valid = false;
    bool heldBlockSupported = false;
    float aspect = 0, fov = 0;
    std::array<float, 16> modelView{}, projection{};
    std::array<FirstPersonVertex, 8> modelVertices{}, cameraVertices{};
    std::array<FirstPersonProjectedVertex, 8> projectedVertices{};
    std::array<std::array<FirstPersonUV, 4>, 6> faceUV{};
    // Conservative raster rectangle [left,top,right,bottom), clipped to viewport.
    std::array<int, 4> rasterBounds{};
    size_t triangleCount = 0;
};

// Empty-hand path from fixed 09df ItemInHandRenderer -> PlayerRenderer ->
// HumanoidModel/Polygon. nullptr pixels still produces diagnostics. Invalid
// dimensions are no-ops; nonfinite state becomes the documented idle default.
// Default is standing Steve (64x32), no hurt/stereo/zoom/skin overrides. The
// existing cuboid topology and software lighting/fill are platform code, not
// a claim of GPU pixel equivalence; see generated/OriginalFirstPerson.manifest.json.
void renderOriginalHand(uint32_t* pixels, int w, int h,
                        const FirstPersonState& state = {},
                        FirstPersonDiagnostics* diagnostics = nullptr);

// Composite after world, before UI: contiguous straight ARGB32 (AARRGGBB).
// view0 uses original empty-hand transforms, actual viewport aspect and 70deg
// hand FOV. walkPhase is intentionally ignored in view0: no fabricated bob or
// swing. view1 remains the existing rear three-quarter CHARACTER PREVIEW (not
// a world third-person camera); its walkPhase is radians, periodic at 2*pi.
// swing is clamped [0,1], nonfinite becomes 0. Other modes, null pixels,
// nonpositive dimensions or dimensions >16384 are no-ops. Skin alpha=0 leaves
// color/depth intact. Thread-local rectangle depth storage is reused when warm.
// heldBlockIndex is the main inventory catalog index, never inferred from walk.
void renderActorOverlay(uint32_t* pixels, int w, int h, int viewMode,
                        float walkPhase, float swing = 0.0f, int heldBlockIndex = -1);

// Independent first-person non-block item path. itemId/aux are CANONICAL item
// identity (not inventory indices or packed world state). Uses ItemIcons' exact
// key lookup; unsupported keys are no-ops, never an empty-hand fallback. Damage
// normalization, if required, belongs to the caller. Same straight ARGB32 and
// viewport guards as renderActorOverlay. Nonfinite swing/equip become 0/1;
// finite values clamp to [0,1]. Does not alter the existing hand/full-cube APIs.
void renderHeldItemOverlay(uint32_t* pixels, int w, int h, int itemId, int aux,
                           float swing = 0.0f, float equip = 1.0f);
