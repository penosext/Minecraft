#include "ItemEntityRenderer.hpp"

#include "ActorRenderer.hpp"
#include "ItemCatalog.hpp"
#include "ItemIcons.hpp"
#include "LegacyCatalog.hpp"
#include "LegacyTextures.hpp"
#include "World.hpp"
#include "WorldRenderer.hpp"

#include <cstdint>
using byte = unsigned char;
#include <Random.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <string>
#include <string_view>
#include <utility>

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kRadians = kPi / 180.0f, kNear = 0.08f;
struct Vec3 { float x, y, z; };
Vec3 subtract(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

struct Camera {
    Vec3 eye, forward, right, up;
    float focal, screenX, screenY;
    Vec3 transform(Vec3 p) const {
        const Vec3 d = subtract(p, eye);
        return {dot(d, right), -dot(d, up), dot(d, forward)};
    }
};
struct Vertex { float x, y, q, uq, vq; };
struct ClipVertex { Vec3 p; float u, v; };
struct Triangle {
    Vertex a, b, c;
    const uint32_t* texture;
    int textureWidth, u0, v0, u1, v1;
    uint32_t tint; // RGB multiplier
    unsigned light; // 0..255
};

// Identity -> what ItemRenderer draws. Cached per id/aux (catalog scans are linear).
enum class Kind { None, Block, Sprite };
struct ItemLook {
    Kind kind = Kind::None;
    HeldModelView model;           // Block3D mesh or terrain-sprite view
    const uint32_t* icon = nullptr; // 16x16, itemicons layout
    uint32_t tint = 0xffffff;
};
const ItemLook& lookOf(int itemId, int aux) {
    thread_local std::map<std::pair<int, int>, ItemLook> cache;
    const auto key = std::make_pair(itemId, aux);
    const auto found = cache.find(key);
    if (found != cache.end()) return found->second;
    ItemLook look;
    const std::string name = "item:" + std::to_string(itemId) + ":" + std::to_string(aux);
    if (const auto* icon = itemicons::findIcon(name.c_str())) {
        // Independent Items (tools, food, materials, door/seed items...).
        look.kind = Kind::Sprite; look.icon = icon->pixels;
    } else {
        const int index = sourceitems::itemIndexForIdAux(itemId, aux);
        const int render = index >= 0 ? sourceitems::kItems[index].renderIndex : -1;
        look.model = heldModelView(render);
        if (look.model.path == HeldBlockPath::Block3D) look.kind = Kind::Block;
        else if (look.model.path == HeldBlockPath::Sprite && look.model.sprite) {
            look.kind = Kind::Sprite; look.icon = look.model.sprite; look.tint = look.model.spriteTint;
        }
    }
    return cache.emplace(key, look).first->second;
}

// ItemRenderer.cpp:66, 109-111: random->setSeed(187) per entity, then three
// nextFloat()s per extra copy. The sequence is fixed, so compute it once.
const std::array<Vec3, 5>& copyOffsets() {
    static const auto offsets = [] {
        std::array<Vec3, 5> out{};
        Random random(187);
        for (int i = 1; i < 5; ++i) {
            const float x = random.nextFloat() * 2 - 1, y = random.nextFloat() * 2 - 1, z = random.nextFloat() * 2 - 1;
            out[size_t(i)] = {x, y, z};
        }
        return out;
    }();
    return offsets;
}

// MobRenderer's occlusion rule: fully opaque cube terrain hides entities.
const std::array<bool, legacy::kBlockCount + 1>& opaqueBlocks() {
    static const auto values = [] {
        std::array<bool, legacy::kBlockCount + 1> result{};
        for (int i = 0; i < legacy::kBlockCount; ++i) {
            const auto& b = legacy::kBlockSpecs[i];
            const std::string_view key(b.key), shape(b.shape);
            result[size_t(i) + 1] = b.alpha >= 0.99f && (shape == "cube" || shape == "grass") && key != "glass" &&
                key != "ice" && key != "water" && key != "calmWater" && key.find("leaves") == std::string_view::npos;
        }
        return result;
    }();
    return values;
}
struct Axis { int step; float delta, next; };
Axis axis(float d, int cell, float origin) {
    if (std::abs(d) < 1e-7f) return {1, 1e20f, 1e20f};
    const int step = d < 0 ? -1 : 1;
    const float delta = std::abs(1 / d);
    return {step, delta, (step > 0 ? cell + 1 - origin : origin - cell) * delta};
}
// Depth (camera z) of the first opaque cell along the pixel ray, or 1e20.
float terrainDepth(const mcworld::World& world, const Camera& camera, float vx, float vy, float limit) {
    const Vec3 d = {camera.forward.x + camera.right.x * vx + camera.up.x * vy, camera.forward.y + camera.up.y * vy,
                    camera.forward.z + camera.right.z * vx + camera.up.z * vy};
    int x = int(std::floor(camera.eye.x)), y = int(std::floor(camera.eye.y)), z = int(std::floor(camera.eye.z));
    Axis ax = axis(d.x, x, camera.eye.x), ay = axis(d.y, y, camera.eye.y), az = axis(d.z, z, camera.eye.z);
    const auto& opaque = opaqueBlocks();
    const auto blocks = [&] { const unsigned id = world.blockAt(x, y, z); return id < opaque.size() && opaque[id]; };
    if (blocks()) return 0;
    for (int i = 0; i < 64; ++i) {
        float t;
        if (ax.next < ay.next && ax.next < az.next) { t = ax.next; ax.next += ax.delta; x += ax.step; }
        else if (ay.next < az.next) { t = ay.next; ay.next += ay.delta; y += ay.step; }
        else { t = az.next; az.next += az.delta; z += az.step; }
        if (t > limit) break;
        if (blocks()) return t;
    }
    return 1e20f;
}

float lightAt(const mcworld::World& world, double x, double y, double z, float daylight) {
    const auto& cache = worldrender::lightingBuffer();
    if (!cache.width || cache.sky.empty()) return .035f + .965f * daylight;
    const int cx = int(std::floor(x)), cy = int(std::floor(y)), cz = int(std::floor(z));
    if (!world.chunkLoaded(cx, cz)) return .035f + .965f * daylight;
    auto sample = cache.sample(cx, cy, cz);
    const auto above = cache.sample(cx, cy + 1, cz);
    sample.sky = std::max(sample.sky, above.sky); sample.block = std::max(sample.block, above.block);
    // WorldRenderer's Dimension brightness ramp (sky scaled by daylight).
    const auto ramp = [](int level) { const float v = 1 - level / 15.0f; return (1 - v) / (v * 3 + 1); };
    return std::max(.035f + .965f * ramp(sample.sky) * daylight, .035f + .965f * ramp(sample.block));
}

struct Builder {
    std::vector<Triangle>& triangles;
    const Camera& camera;
    void quad(const std::array<Vec3, 4>& world, const std::array<std::array<float, 2>, 4>& uv,
              const uint32_t* texture, int textureWidth, int u0, int v0, int u1, int v1, uint32_t tint, float light) {
        std::array<ClipVertex, 4> input{};
        for (int i = 0; i < 4; ++i) input[size_t(i)] = {camera.transform(world[size_t(i)]), uv[size_t(i)][0], uv[size_t(i)][1]};
        // Face shade from the world-space normal (Tesselator-era 1 top, .8 Z, .6 X;
        // bottom .5). Both sides are drawn; the z-buffer resolves visibility.
        const Vec3 n = cross(subtract(world[1], world[0]), subtract(world[2], world[0]));
        const float len2 = dot(n, n);
        if (len2 < 1e-14f) return;
        const float ny = n.y * n.y / len2, nz = n.z * n.z / len2, nx = n.x * n.x / len2;
        const float shade = ny * (n.y > 0 ? 1.0f : .5f) + nz * .8f + nx * .6f;
        const unsigned lit = unsigned(std::clamp(light * shade, 0.0f, 1.0f) * 255 + .5f);
        std::array<ClipVertex, 8> out{};
        int count = 0;
        for (int i = 0; i < 4; ++i) {
            const ClipVertex& a = input[size_t(i)];
            const ClipVertex& b = input[size_t((i + 1) % 4)];
            const bool ia = a.p.z >= kNear, ib = b.p.z >= kNear;
            if (ia) out[size_t(count++)] = a;
            if (ia != ib) {
                const float t = (kNear - a.p.z) / (b.p.z - a.p.z);
                out[size_t(count++)] = {{a.p.x + t * (b.p.x - a.p.x), a.p.y + t * (b.p.y - a.p.y), kNear},
                                        a.u + t * (b.u - a.u), a.v + t * (b.v - a.v)};
            }
        }
        if (count < 3) return;
        const auto project = [&](const ClipVertex& v) {
            const float q = 1 / v.p.z;
            return Vertex{camera.screenX + camera.focal * v.p.x * q, camera.screenY + camera.focal * v.p.y * q,
                          q, v.u * q, v.v * q};
        };
        for (int i = 1; i + 1 < count; ++i)
            triangles.push_back({project(out[0]), project(out[size_t(i)]), project(out[size_t(i + 1)]),
                                 texture, textureWidth, u0, v0, u1, v1, tint, lit});
    }
};

float edge(const Vertex& a, const Vertex& b, float x, float y) { return (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x); }

uint32_t shadeTexel(uint32_t texel, uint32_t tint, unsigned light) {
    uint32_t result = 0xff000000u;
    for (int shift : {0, 8, 16}) {
        const unsigned c = (texel >> shift) & 255, t = (tint >> shift) & 255;
        result |= ((c * t * light + 32512) / 65025) << shift;
    }
    return result;
}

void rasterize(uint32_t* pixels, int w, int h, const mcworld::World& world, const Camera& camera,
               const std::vector<Triangle>& triangles) {
    if (triangles.empty()) return;
    float minX = float(w), minY = float(h), maxX = 0, maxY = 0, maxDepth = 0;
    for (const auto& t : triangles) for (const auto& v : {t.a, t.b, t.c}) {
        minX = std::min(minX, v.x); maxX = std::max(maxX, v.x);
        minY = std::min(minY, v.y); maxY = std::max(maxY, v.y);
        maxDepth = std::max(maxDepth, 1 / v.q);
    }
    const int left = int(std::clamp(std::floor(minX), 0.0f, float(w)));
    const int top = int(std::clamp(std::floor(minY), 0.0f, float(h)));
    const int right = int(std::clamp(std::ceil(maxX), 0.0f, float(w)));
    const int bottom = int(std::clamp(std::ceil(maxY), 0.0f, float(h)));
    if (right <= left || bottom <= top) return;
    const int stride = right - left, rows = bottom - top;
    const int tile = std::max(4, (w + 239) / 240);
    thread_local std::vector<float> depth, terrain;
    depth.assign(size_t(stride) * rows, 0);
    const int tileStride = (stride + tile - 1) / tile;
    terrain.assign(size_t(tileStride) * ((rows + tile - 1) / tile), -1);
    for (Triangle tri : triangles) {
        float area = edge(tri.a, tri.b, tri.c.x, tri.c.y);
        if (std::abs(area) < 1e-7f) continue;
        if (area < 0) { std::swap(tri.b, tri.c); area = -area; }
        const int x0 = int(std::clamp(std::floor(std::min({tri.a.x, tri.b.x, tri.c.x})), float(left), float(right)));
        const int x1 = int(std::clamp(std::ceil(std::max({tri.a.x, tri.b.x, tri.c.x})), float(left), float(right)));
        const int y0 = int(std::clamp(std::floor(std::min({tri.a.y, tri.b.y, tri.c.y})), float(top), float(bottom)));
        const int y1 = int(std::clamp(std::ceil(std::max({tri.a.y, tri.b.y, tri.c.y})), float(top), float(bottom)));
        if (x1 <= x0 || y1 <= y0) continue;
        const float inv = 1 / area;
        const float ax = -(tri.c.y - tri.b.y), ay = tri.c.x - tri.b.x;
        const float bx = -(tri.a.y - tri.c.y), by = tri.a.x - tri.c.x;
        const float cx = -(tri.b.y - tri.a.y), cy = tri.b.x - tri.a.x;
        float waRow = edge(tri.b, tri.c, x0 + .5f, y0 + .5f);
        float wbRow = edge(tri.c, tri.a, x0 + .5f, y0 + .5f);
        float wcRow = edge(tri.a, tri.b, x0 + .5f, y0 + .5f);
        const float dq = (ax * tri.a.q + bx * tri.b.q + cx * tri.c.q) * inv;
        const float du = (ax * tri.a.uq + bx * tri.b.uq + cx * tri.c.uq) * inv;
        const float dv = (ax * tri.a.vq + bx * tri.b.vq + cx * tri.c.vq) * inv;
        for (int y = y0; y < y1; ++y) {
            float wa = waRow, wb = wbRow, wc = wcRow;
            float q = (wa * tri.a.q + wb * tri.b.q + wc * tri.c.q) * inv;
            float uq = (wa * tri.a.uq + wb * tri.b.uq + wc * tri.c.uq) * inv;
            float vq = (wa * tri.a.vq + wb * tri.b.vq + wc * tri.c.vq) * inv;
            size_t zi = size_t(y - top) * stride + size_t(x0 - left), pi = size_t(y) * w + size_t(x0);
            for (int x = x0; x < x1; ++x, ++zi, ++pi) {
                if (wa >= 0 && wb >= 0 && wc >= 0 && q > depth[zi]) {
                    const float z = 1 / q;
                    const int u = std::clamp(int(uq * z), tri.u0, tri.u1), v = std::clamp(int(vq * z), tri.v0, tri.v1);
                    const uint32_t texel = tri.texture[v * tri.textureWidth + u];
                    if ((texel >> 24) >= 128) {
                        const int tx = (x - left) / tile, ty = (y - top) / tile;
                        float& terrainZ = terrain[size_t(ty) * tileStride + tx];
                        if (terrainZ < 0) {
                            const float px = std::min(float(w) - .5f, float(left + tx * tile) + tile * .5f);
                            const float py = std::min(float(h) - .5f, float(top + ty * tile) + tile * .5f);
                            terrainZ = terrainDepth(world, camera, (px - camera.screenX) / camera.focal,
                                                    (camera.screenY - py) / camera.focal, maxDepth + .1f);
                        }
                        if (z < terrainZ - .002f) { pixels[pi] = shadeTexel(texel, tri.tint, tri.light); depth[zi] = q; }
                    }
                }
                wa += ax; wb += bx; wc += cx; q += dq; uq += du; vq += dv;
            }
            waRow += ay; wbRow += by; wcRow += cy;
        }
    }
}
} // namespace

void renderItemOverlay(uint32_t* pixels, int w, int h, const mcworld::World& world, float yaw, float pitch,
                       const std::vector<mcscene::ItemEntity>& items, float daylight, float partialTick) {
    if (!pixels || w <= 0 || h <= 0 || w > 16384 || h > 16384 || !world.loaded() || items.empty() ||
        !std::isfinite(yaw) || !std::isfinite(pitch)) return;
    yaw = std::remainder(yaw, 360.0f) * kRadians;
    pitch = std::clamp(pitch, -80.0f, 80.0f) * kRadians;
    daylight = std::isfinite(daylight) ? std::clamp(daylight, 0.0f, 1.0f) : 1;
    const float a = std::isfinite(partialTick) ? std::clamp(partialTick, 0.0f, 1.0f) : 0;
    const float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
    const Camera camera{{world.playerX(), world.playerY() + 1.62f, world.playerZ()},
                        {sy * cp, sp, cy * cp}, {cy, 0, -sy}, {-sy * sp, cp, -cy * sp},
                        worldrender::focalLength(w, h), w * .5f - .5f, h * .5f - .5f};
    thread_local std::vector<Triangle> triangles;
    triangles.clear();
    Builder builder{triangles, camera};
    const auto& offsets = copyOffsets();
    for (const auto& item : items) {
        if (item.count <= 0 || !std::isfinite(item.x) || !std::isfinite(item.y) || !std::isfinite(item.z)) continue;
        // Entity y = bb.y0 + heightOffset .125 (ItemEntity.cpp:30).
        const float ex = float(item.x), ey = float(item.y) + .125f, ez = float(item.z);
        const Vec3 d = subtract({ex, ey, ez}, camera.eye);
        // Entity::shouldRenderAtSqrDistance: (bb size .25 * 64)^2 = 16 blocks.
        if (dot(d, d) >= 16.0f * 16.0f || dot(d, camera.forward) < -1) continue;
        const ItemLook& look = lookOf(item.itemId, item.aux);
        if (look.kind == Kind::None) continue;
        const float t = float(item.age) + a;
        const float bob = std::sin(t / 10.0f + item.bobOffs) * .1f + .1f;
        const float spin = t / 20.0f + item.bobOffs; // radians (ItemRenderer.cpp:72 * RADDEG)
        const float cs = std::cos(spin), ss = std::sin(spin);
        // glRotatef(spin, 0, 1, 0) on (x, z).
        const auto rotate = [&](float x, float z) { return std::make_pair(cs * x + ss * z, -ss * x + cs * z); };
        const float light = lightAt(world, item.x, item.y + .125, item.z, daylight);
        if (look.kind == Kind::Block) {
            const int copies = item.count > 40 ? 5 : item.count > 20 ? 4 : item.count > 5 ? 3 : item.count > 1 ? 2 : 1;
            constexpr float s = .25f; // ItemRenderer.cpp:96
            for (int c = 0; c < copies; ++c) {
                // Offsets are (rand*2-1)*.2/s in scaled space = *.2 in world space.
                const Vec3 o = c ? Vec3{offsets[size_t(c)].x * .2f, offsets[size_t(c)].y * .2f, offsets[size_t(c)].z * .2f}
                                 : Vec3{0, 0, 0};
                for (size_t qi = 0; qi < look.model.quadCount; ++qi) {
                    const HeldModelQuad& q = look.model.quads[qi];
                    std::array<Vec3, 4> p{};
                    std::array<std::array<float, 2>, 4> uv{};
                    for (int k = 0; k < 4; ++k) {
                        float lx = q.p[k][0] - .5f, ly = q.p[k][1] - .5f, lz = q.p[k][2] - .5f;
                        // renderTile's glRotatef(90, 0, 1, 0) branch: (x, z) -> (z, -x).
                        if (look.model.rotated) { const float nx = lz, nz = -lx; lx = nx; lz = nz; }
                        const auto r = rotate(lx * s + o.x, lz * s + o.z);
                        p[size_t(k)] = {ex + r.first, ey + bob + (ly * s + o.y), ez + r.second};
                        uv[size_t(k)] = {q.uv[k][0], q.uv[k][1]};
                    }
                    builder.quad(p, uv, legacy::kAtlasPixels, legacy::kAtlasWidth, q.u0, q.v0, q.u1, q.v1, q.tint, light);
                }
            }
        } else {
            // renderItemBillboard (fancy): scale 1/2, quad [-.5,.5] x [-.25,.75],
            // copies stacked (width + margin) / 16 apart along the spun z.
            const int copies = item.count < 2 ? 1 : item.count < 16 ? 2 : item.count < 32 ? 3 : 4;
            constexpr float scale = .5f, spacing = (1 / 16.0f + .35f / 16.0f) * scale;
            const float z0 = -spacing * float(copies) / 2;
            for (int c = 0; c < copies; ++c) {
                const float lz = z0 + spacing * float(c + 1);
                std::array<Vec3, 4> p{};
                const float corners[4][2] = {{-.5f, -.25f}, {.5f, -.25f}, {.5f, .75f}, {-.5f, .75f}};
                // Icon row 0 is the top; the plane spins, so both faces are seen.
                const std::array<std::array<float, 2>, 4> uv{{{0, 16}, {16, 16}, {16, 0}, {0, 0}}};
                for (int k = 0; k < 4; ++k) {
                    const auto r = rotate(corners[k][0] * scale, lz);
                    p[size_t(k)] = {ex + r.first, ey + bob + corners[k][1] * scale, ez + r.second};
                }
                builder.quad(p, uv, look.icon, itemicons::kIconSize, 0, 0, 15, 15, look.tint, light);
            }
        }
    }
    rasterize(pixels, w, h, world, camera, triangles);
}
