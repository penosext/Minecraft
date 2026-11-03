#include "SoftwareRenderer.hpp"
#include "LegacyBlock.hpp"
#include "LegacyTextures.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace {
struct ScreenVertex {
    float x, y, depth;
    legacy::Vertex vertex;
};

struct Camera {
    float sinYaw, cosYaw, sinPitch, cosPitch;
};

ScreenVertex project(const legacy::Vertex& v, int w, int h, const Camera& camera)
{
    const float scale = std::min(w * 0.3f, h * 0.50f);
    const float x = v.x - 0.5f, y = v.y - 0.5f, z = v.z - 0.5f;
    const float horizontal = camera.cosYaw * x - camera.sinYaw * z;
    const float forward = camera.sinYaw * x + camera.cosYaw * z;
    const float vertical = camera.cosPitch * y - camera.sinPitch * forward;
    const float depth = camera.cosPitch * forward + camera.sinPitch * y;
    return {w * 0.5f + horizontal * scale,
            h * 0.5f - vertical * scale, depth, v};
}

void rasterizeQuad(uint32_t* pixels, std::vector<float>& depths,
                   int w, int h, const legacy::Vertex* vertices, const Camera& camera)
{
    ScreenVertex p[4] = {project(vertices[0], w, h, camera), project(vertices[1], w, h, camera),
                         project(vertices[2], w, h, camera), project(vertices[3], w, h, camera)};
    const float ax = p[1].x - p[0].x, ay = p[1].y - p[0].y;
    const float bx = p[3].x - p[0].x, by = p[3].y - p[0].y;
    const float determinant = ax * by - ay * bx;
    if (std::abs(determinant) < 0.001f) return;
    const int minX = std::max(0, int(std::floor(std::min({p[0].x, p[1].x, p[2].x, p[3].x}))));
    const int maxX = std::min(w - 1, int(std::ceil(std::max({p[0].x, p[1].x, p[2].x, p[3].x}))));
    const int minY = std::max(0, int(std::floor(std::min({p[0].y, p[1].y, p[2].y, p[3].y}))));
    const int maxY = std::min(h - 1, int(std::ceil(std::max({p[0].y, p[1].y, p[2].y, p[3].y}))));
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const float dx = x + 0.5f - p[0].x, dy = y + 0.5f - p[0].y;
            const float a = (dx * by - dy * bx) / determinant;
            const float b = (ax * dy - ay * dx) / determinant;
            if (a < 0 || a >= 1 || b < 0 || b >= 1) continue;
            const float depth = p[0].depth + a * (p[1].depth - p[0].depth) + b * (p[3].depth - p[0].depth);
            const size_t offset = size_t(y) * w + x;
            if (depth + 0.00001f < depths[offset]) continue;
            const auto interpolate = [&](float legacy::Vertex::*field) {
                return vertices[0].*field + a * (vertices[1].*field - vertices[0].*field)
                       + b * (vertices[3].*field - vertices[0].*field);
            };
            const int u = std::clamp(int(interpolate(&legacy::Vertex::u) * legacy::kAtlasWidth), 0, legacy::kAtlasWidth - 1);
            const int v = std::clamp(int(interpolate(&legacy::Vertex::v) * legacy::kAtlasHeight), 0, legacy::kAtlasHeight - 1);
            const uint32_t texel = legacy::kAtlasPixels[v * legacy::kAtlasWidth + u];
            const int alpha = std::clamp(int((texel >> 24) * interpolate(&legacy::Vertex::a)), 0, 255);
            if (!alpha) continue;
            const int r = std::clamp(int(((texel >> 16) & 255) * interpolate(&legacy::Vertex::r)), 0, 255);
            const int g = std::clamp(int(((texel >> 8) & 255) * interpolate(&legacy::Vertex::g)), 0, 255);
            const int blue = std::clamp(int((texel & 255) * interpolate(&legacy::Vertex::b)), 0, 255);
            const uint32_t old = pixels[offset];
            pixels[offset] = ARGB(255, (r * alpha + int((old >> 16) & 255) * (255 - alpha)) / 255,
                                 (g * alpha + int((old >> 8) & 255) * (255 - alpha)) / 255,
                                 (blue * alpha + int(old & 255) * (255 - alpha)) / 255);
            depths[offset] = depth;
        }
    }
}
}

void renderMinecraftFrame(uint32_t* argb, int w, int h, int /*frame*/, float yaw, float pitch, int blockIndex)
{
    if (!argb || w <= 0 || h <= 0) return;
    if (!std::isfinite(yaw) || !std::isfinite(pitch)) return;
    constexpr float radians = 3.14159265358979323846f / 180.0f;
    yaw = std::remainder(yaw, 360.0f) * radians;
    pitch = std::clamp(pitch, -80.0f, 80.0f) * radians;
    const Camera camera{std::sin(yaw), std::cos(yaw), std::sin(pitch), std::cos(pitch)};
    for (int y = 0; y < h; ++y) {
        const uint32_t sky = ARGB(255, 125 + 30 * y / h, 183 + 25 * y / h, 232);
        std::fill(argb + size_t(y) * w, argb + size_t(y + 1) * w, sky);
    }
    const auto& vertices = legacy::buildTerrainMesh(blockIndex);
    std::vector<float> depths(size_t(w) * h, -std::numeric_limits<float>::infinity());
    std::vector<size_t> quads;
    for (size_t i = 0; i + 3 < vertices.size(); i += 4) quads.push_back(i);
    if (!vertices.empty() && vertices[0].a < 1.0f) {
        const auto depth = [&](size_t offset) {
            float value = 0;
            for (size_t j = 0; j < 4; ++j) value += project(vertices[offset + j], w, h, camera).depth;
            return value;
        };
        std::stable_sort(quads.begin(), quads.end(), [&](size_t a, size_t b) { return depth(a) < depth(b); });
    }
    for (const size_t offset : quads)
        rasterizeQuad(argb, depths, w, h, vertices.data() + offset, camera);
}
