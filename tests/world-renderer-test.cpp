// The frozen, pre-optimization renderer is shared with world-benchmark.cpp.
// Define WORLD_RENDERER_REFERENCE_ONLY to omit the test driver.
#include "World.hpp"
#include "WorldRenderer.hpp"
#include "LegacyBlock.hpp"
#include "LegacyCatalog.hpp"
#include "LegacyTextures.hpp"
#include "SoftwareRenderer.hpp"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <string_view>
#include <vector>
#include <unistd.h>

namespace renderer_reference {
struct Vector { float x, y, z; };
struct TextureSet { const legacy::Icon* faces[6]; };
const std::vector<TextureSet>& textures()
{
    static const auto result = [] {
        std::vector<TextureSet> sets;
        legacy::IconRegister registry;
        for (const auto& block : legacy::kBlockSpecs) {
            TextureSet set{};
            for (int face = 0; face < 6; ++face) set.faces[face] = registry.registerIcon(block.faces[face]);
            sets.push_back(set);
        }
        return sets;
    }();
    return result;
}
uint32_t sample(const legacy::Icon* icon, float u, float v)
{
    const float epsilon = 0.0001f;
    const float tu = icon->u0 + (icon->u1 - icon->u0) * std::clamp(u, epsilon, 1.0f - epsilon);
    const float tv = icon->v0 + (icon->v1 - icon->v0) * std::clamp(v, epsilon, 1.0f - epsilon);
    const int x = std::clamp(int(tu * legacy::kAtlasWidth), 0, legacy::kAtlasWidth - 1);
    const int y = std::clamp(int(tv * legacy::kAtlasHeight), 0, legacy::kAtlasHeight - 1);
    return legacy::kAtlasPixels[y * legacy::kAtlasWidth + x];
}
float fraction(float value) { return value - std::floor(value); }
uint32_t trace(const mcworld::World& world, Vector origin, Vector direction)
{
    const float skyMix = std::clamp(0.5f + direction.y * 0.35f, 0.0f, 1.0f);
    const float skyR = 150 - 45 * skyMix, skyG = 204 - 34 * skyMix, skyB = 239;
    int x = int(std::floor(origin.x)), y = int(std::floor(origin.y)), z = int(std::floor(origin.z));
    const int sx = direction.x < 0 ? -1 : 1, sy = direction.y < 0 ? -1 : 1, sz = direction.z < 0 ? -1 : 1;
    const float dx = std::abs(direction.x) > 0.000001f ? std::abs(1 / direction.x) : 1e20f;
    const float dy = std::abs(direction.y) > 0.000001f ? std::abs(1 / direction.y) : 1e20f;
    const float dz = std::abs(direction.z) > 0.000001f ? std::abs(1 / direction.z) : 1e20f;
    float tx = (sx > 0 ? x + 1 - origin.x : origin.x - x) * dx;
    float ty = (sy > 0 ? y + 1 - origin.y : origin.y - y) * dy;
    float tz = (sz > 0 ? z + 1 - origin.z : origin.z - z) * dz;
    const auto& sets = textures();
    static const legacy::Icon* overlay = legacy::IconRegister().registerIcon(L"grass_side_overlay");
    float red = 0, green = 0, blue = 0, transmission = 1;
    for (int step = 0; step < 180; ++step) {
        int face;
        float distance;
        if (tx < ty && tx < tz) { distance = tx; tx += dx; x += sx; face = sx > 0 ? 4 : 5; }
        else if (ty < tz) { distance = ty; ty += dy; y += sy; face = sy > 0 ? 0 : 1; }
        else { distance = tz; tz += dz; z += sz; face = sz > 0 ? 2 : 3; }
        if (distance > 40 || x < 0 || z < 0 || y < 0 ||
            x >= mcworld::Width || z >= mcworld::Depth || y >= mcworld::Height) break;
        const uint16_t voxel = world.blockAt(x, y, z);
        if (!voxel || voxel > legacy::kBlockCount) continue;
        const auto& block = legacy::kBlockSpecs[voxel - 1];
        const Vector hit{origin.x + direction.x * distance,
                         origin.y + direction.y * distance, origin.z + direction.z * distance};
        float u, v;
        if (face <= 1) { u = fraction(hit.x); v = fraction(hit.z); }
        else { u = face <= 3 ? fraction(hit.x) : fraction(hit.z); v = 1 - fraction(hit.y); }
        const uint32_t texel = sample(sets[voxel - 1].faces[face], u, v);
        float alpha = (texel >> 24) / 255.0f;
        if (alpha < 0.1f) continue;
        const bool grass = std::string_view(block.shape) == "grass";
        const bool water = std::string_view(block.key) == "water" || std::string_view(block.key) == "calmWater";
        float tintR = ((block.color >> 16) & 255) / 255.0f;
        float tintG = ((block.color >> 8) & 255) / 255.0f;
        float tintB = (block.color & 255) / 255.0f;
        if (grass && face != 1) tintR = tintG = tintB = 1;
        float r = ((texel >> 16) & 255) * tintR;
        float g = ((texel >> 8) & 255) * tintG;
        float b = (texel & 255) * tintB;
        if (grass && face >= 2) {
            const uint32_t top = sample(overlay, u, v);
            const float a = (top >> 24) / 255.0f;
            r = r * (1 - a) + ((top >> 16) & 255) * ((block.color >> 16) & 255) / 255.0f * a;
            g = g * (1 - a) + ((top >> 8) & 255) * ((block.color >> 8) & 255) / 255.0f * a;
            b = b * (1 - a) + (top & 255) * (block.color & 255) / 255.0f * a;
        }
        const float shade = face == 1 ? 1.0f : (face == 0 ? 0.5f : (face <= 3 ? 0.82f : 0.65f));
        const float fog = std::clamp((distance - 23) / 17, 0.0f, 0.9f);
        r = r * shade * (1 - fog) + skyR * fog;
        g = g * shade * (1 - fog) + skyG * fog;
        b = b * shade * (1 - fog) + skyB * fog;
        alpha *= water ? 0.55f : block.alpha;
        red += transmission * alpha * r;
        green += transmission * alpha * g;
        blue += transmission * alpha * b;
        transmission *= 1 - alpha;
        if (transmission < 0.02f) break;
    }
    return ARGB(255, std::clamp(int(red + transmission * skyR), 0, 255),
                     std::clamp(int(green + transmission * skyG), 0, 255),
                     std::clamp(int(blue + transmission * skyB), 0, 255));
}
void render(uint32_t* pixels, int width, int height,
            const mcworld::World& world, float yaw, float pitch, int pixelStep)
{
    if (!pixels || width <= 0 || height <= 0 || !world.loaded()) return;
    constexpr float radians = 3.14159265358979323846f / 180;
    yaw = std::remainder(yaw, 360.0f) * radians;
    pitch = std::clamp(pitch, -80.0f, 80.0f) * radians;
    const float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
    const Vector forward{sy * cp, sp, cy * cp}, right{cy, 0, -sy}, up{-sy * sp, cp, -cy * sp};
    const Vector eye{world.playerX(), world.playerY() + 1.62f, world.playerZ()};
    const int stride = std::clamp(pixelStep, 1, std::max(width, height));
    for (int y = 0; y < height; y += stride) {
        const float vy = (1 - (y + 1.0f) / height * 2) * height / width;
        for (int x = 0; x < width; x += stride) {
            const float vx = (x + 1.0f) / width * 2 - 1;
            const Vector direction{forward.x + right.x * vx + up.x * vy,
                                   forward.y + up.y * vy, forward.z + right.z * vx + up.z * vy};
            const uint32_t color = trace(world, eye, direction);
            for (int oy = 0; oy < stride && y + oy < height; ++oy)
                for (int ox = 0; ox < stride && x + ox < width; ++ox)
                    pixels[size_t(y + oy) * width + x + ox] = color;
        }
    }
    const int cx = width / 2, centerY = height / 2;
    for (int offset = -4; offset <= 4; ++offset) {
        if (cx + offset >= 0 && cx + offset < width) pixels[size_t(centerY) * width + cx + offset] = 0xffeeeeee;
        if (centerY + offset >= 0 && centerY + offset < height) pixels[size_t(centerY + offset) * width + cx] = 0xffeeeeee;
    }
}
struct Difference {
    size_t pixels = 0;
    int maxChannel = 0;
};
Difference difference(const std::vector<uint32_t>& expected, const std::vector<uint32_t>& actual)
{
    assert(expected.size() == actual.size());
    Difference result;
    for (size_t i = 0; i < expected.size(); ++i) {
        result.pixels += expected[i] != actual[i];
        for (int shift : {0, 8, 16, 24})
            result.maxChannel = std::max(result.maxChannel,
                std::abs(int((expected[i] >> shift) & 255) - int((actual[i] >> shift) & 255)));
    }
    return result;
}
} // namespace renderer_reference

#ifndef WORLD_RENDERER_REFERENCE_ONLY
namespace {
struct TemporaryRoot {
    char path[40] = "/tmp/mc-render-test.XXXXXX";
    TemporaryRoot() { if (!mkdtemp(path)) std::abort(); }
    ~TemporaryRoot() { std::error_code error; std::filesystem::remove_all(path, error); }
};
constexpr uint32_t guard = 0x12345678;
size_t comparisonCount = 0, differingPixels = 0;
int maxChannelError = 0;
void compare(const mcworld::World& world, int width, int height, float yaw, float pitch, int step)
{
    std::vector<uint32_t> actual(size_t(width) * height + 2, guard), expected = actual;
    // The frozen renderer intentionally retains horizontal FOV 90 and no
    // skylight/AO/celestials. Production defaults are covered by lighting-celestial-test.
    worldrender::LightingSettings lighting;
    lighting.enabled = false;
    lighting.ambientOcclusion = false;
    lighting.legacyProjection = true;
    renderWorldFrame(actual.data() + 1, width, height, world, yaw, pitch, step, 1, -1, lighting);
    renderer_reference::render(expected.data() + 1, width, height, world, yaw, pitch, step);
    const auto delta = renderer_reference::difference(expected, actual);
    ++comparisonCount;
    differingPixels += delta.pixels;
    maxChannelError = std::max(maxChannelError, delta.maxChannel);
    if (delta.maxChannel > 1)
        std::cerr << "Mismatch " << width << 'x' << height << " yaw=" << yaw << " pitch=" << pitch
                  << " step=" << step << " pixels=" << delta.pixels << " maxChannel=" << delta.maxChannel << '\n';
    // Floating-point contraction can change a final channel's rounding by 1.
    assert(delta.maxChannel <= 1);
    assert(actual.front() == guard && actual.back() == guard);
    for (size_t i = 1; i + 1 < actual.size(); ++i) assert((actual[i] >> 24) == 255);
    const int cx = width / 2, cy = height / 2;
    assert(actual[1 + size_t(cy) * width + cx] == 0xffeeeeee);
}
}
int main(int argc, char** argv)
{
    TemporaryRoot root;
    mcworld::World world;
    world.create(12345, root.path, false);
    constexpr int width = 960, height = 266;
    std::vector<uint32_t> pixels(size_t(width) * height + 2, guard);
    const auto start = std::chrono::steady_clock::now();
    renderWorldFrame(pixels.data() + 1, width, height, world, 0, -10);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    assert(pixels.front() == guard && pixels.back() == guard);
    std::set<uint32_t> colors(pixels.begin() + 1, pixels.end() - 1);
    assert(colors.size() > 100);
    auto explicitDefault = pixels;
    renderWorldFrame(explicitDefault.data() + 1, width, height, world, 0, -10, 3);
    assert(explicitDefault == pixels);
    auto* voxels = const_cast<uint16_t*>(world.blockData());
    const size_t cells = size_t(mcworld::Width) * mcworld::Depth * mcworld::Height;
    std::vector<uint16_t> original(voxels, voxels + cells);
    const int ex = int(std::floor(world.playerX())), ey = int(std::floor(world.playerY() + 1.62f));
    const int ez = int(std::floor(world.playerZ()));
    const auto cell = [](int x, int y, int z) { return (size_t(y) * mcworld::Depth + z) * mcworld::Width + x; };
    const auto blockId = [](std::string_view key) {
        for (int i = 0; i < legacy::kBlockCount; ++i)
            if (key == legacy::kBlockSpecs[i].key) return uint16_t(i + 1);
        std::abort();
    };
    // Compare the frozen implementation only where its semantics are still
    // applicable: opaque full cubes at near range, before either fog begins.
    std::fill(voxels, voxels + cells, blockId("stone"));
    voxels[cell(ex, ey, ez)] = 0;
    for (int step : {1, 2, 3, 7}) compare(world, width, height, 0, -10, step);
    for (float yaw : {0.0f, 45.0f, 90.0f, 180.0f, 270.0f, -360.0f, 720.0f})
        for (float pitch : {-80.0f, -30.0f, 0.0f, 80.0f}) compare(world, 163, 91, yaw, pitch, 2);
    for (int w : {1, 2, 7, 31}) for (int h : {1, 3, 14, 29})
        for (int step : {-3, 0, 1, 2, 3, 8, 1000}) compare(world, w, h, 13, -19, step);
    // Opaque/translucent/cutout blocks in every face orientation, plus layered
    // water/leaf rays. This changes only a private in-memory test fixture.
    // blockData() points to nonconst storage owned by this nonconst World.
    for (const char* key : {"grass", "stone"}) {
        int id = 0;
        for (int i = 0; i < legacy::kBlockCount; ++i)
            if (std::string_view(legacy::kBlockSpecs[i].key) == key) id = i + 1;
        assert(id);
        std::fill(voxels, voxels + cells, uint16_t(id));
        voxels[cell(ex, ey, ez)] = 0;
        for (float yaw : {0.0f, 90.0f, 180.0f, 270.0f})
            for (float pitch : {-80.0f, 0.0f, 80.0f}) compare(world, 65, 43, yaw, pitch, 2);
    }
    // The frozen renderer treats every block as a cube: compare only actual
    // full cubes (including non-unit alpha). Special shapes have direct geometry
    // and DDA coverage in structure-renderer-test, not a false cube oracle here.
    for (int id = 1; id <= legacy::kBlockCount; ++id) {
        const auto& spec = legacy::kBlockSpecs[id - 1];
        if (!worldrender::buildGeometry(spec).fullCube || std::string_view(spec.shape) == "leaves" || spec.alpha < 1) continue;
        std::fill(voxels, voxels + cells, uint16_t(id));
        voxels[cell(ex, ey, ez)] = 0;
        for (float yaw : {0.0f, 90.0f, 180.0f, 270.0f})
            compare(world, 33, 19, yaw, -31, 2);
        compare(world, 33, 19, 25, 80, 2);
    }
    // Euclidean view distance, translucent shapes and updated fog differ from
    // the frozen algorithm; production coverage lives in the lighting/shape suites.
    std::fill(voxels, voxels + cells, 0);
    compare(world, 97, 51, 0, 0, 3);
    // Invalid voxel ids remain air, just like the old renderer.
    std::fill(voxels, voxels + cells, uint16_t(65535));
    compare(world, 97, 51, 90, 15, 3);
    std::copy(original.begin(), original.end(), voxels);
    for (int frame = 0; frame < 20; ++frame) world.update(.1f, 1, .25f, false, 40);
    auto live = pixels;
    renderWorldFrame(live.data() + 1, width, height, world, 40, -15, 2);
    assert(live.front() == guard && live.back() == guard);
    // Actual edits invalidate production lighting, not the historical oracle.
    world.mine(40, -60);
    renderWorldFrame(live.data() + 1, width, height, world, 40, -60, 2);
    assert(live.front() == guard && live.back() == guard);
    mcworld::World unloaded;
    uint32_t untouched = guard;
    renderWorldFrame(&untouched, 1, 1, unloaded, 0, 0);
    renderWorldFrame(nullptr, 1, 1, world, 0, 0);
    renderWorldFrame(&untouched, 0, 1, world, 0, 0);
    renderWorldFrame(&untouched, 1, -1, world, 0, 0);
    renderWorldFrame(&untouched, 1, 1, world, std::numeric_limits<float>::quiet_NaN(), 0);
    renderWorldFrame(&untouched, 1, 1, world, 0, std::numeric_limits<float>::infinity());
    assert(untouched == guard);
    if (argc > 1) {
        std::ofstream output(argv[1], std::ios::binary);
        output << "P6\n" << width << ' ' << height << "\n255\n";
        for (size_t i = 1; i + 1 < pixels.size(); ++i) {
            const char rgb[] = {char(pixels[i] >> 16), char(pixels[i] >> 8), char(pixels[i])};
            output.write(rgb, 3);
        }
    }
    std::cout << "World render/reference passed: " << colors.size() << " colors, cold default "
              << elapsed << " ms; comparisons=" << comparisonCount << " differing_pixels=" << differingPixels
              << " max_channel_error=" << maxChannelError
              << "; angle/step/boundary/water/leaf/grass/live-edit checks passed\n";
}
#endif
