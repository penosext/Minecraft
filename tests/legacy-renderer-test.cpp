#include "LegacyBlock.hpp"
#include "SoftwareRenderer.hpp"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    const auto vertices = legacy::buildGrassMesh();
    assert(vertices.size() == 40);
    for (const auto& vertex : vertices) {
        assert(vertex.x == 0 || vertex.x == 1);
        assert(vertex.y == 0 || vertex.y == 1);
        assert(vertex.z == 0 || vertex.z == 1);
        assert(vertex.u >= 0 && vertex.u <= 1);
        assert(vertex.v >= 0 && vertex.v <= 1);
    }
    for (size_t i = 4; i < 8; ++i) {
        assert(vertices[i].y == 1);
        assert(std::abs(vertices[i].r - 124.0f / 255) < 0.00001f);
        assert(std::abs(vertices[i].g - 189.0f / 255) < 0.00001f);
        assert(std::abs(vertices[i].b - 107.0f / 255) < 0.00001f);
    }
    legacy::IconRegister registry;
    const auto* side = registry.registerIcon(L"grass_side");
    const auto* overlay = registry.registerIcon(L"grass_side_overlay");
    assert(vertices[8].u >= side->u0 && vertices[8].u <= side->u1);
    assert(vertices[12].u >= overlay->u0 && vertices[12].u <= overlay->u1);
    assert(std::abs(vertices[8].r - 0.8f) < 0.00001f);
    assert(std::abs(vertices[12].r - 0.8f * 124 / 255) < 0.00001f);
    for (const auto size : {1, 2, 64, 266}) {
        const int w = size == 266 ? 960 : size;
        const int h = size;
        std::vector<uint32_t> pixels(size_t(w) * h + 2, 0x12345678);
        renderMinecraftFrame(pixels.data() + 1, w, h, 0);
        assert(pixels.front() == 0x12345678 && pixels.back() == 0x12345678);
        for (size_t i = 1; i + 1 < pixels.size(); ++i)
            assert((pixels[i] >> 24) == 255);
        if (size == 266 && argc > 1) {
            std::ofstream output(argv[1], std::ios::binary);
            output << "P6\n" << w << " " << h << "\n255\n";
            for (size_t i = 1; i + 1 < pixels.size(); ++i) {
                const char rgb[] = {char(pixels[i] >> 16), char(pixels[i] >> 8), char(pixels[i])};
                output.write(rgb, 3);
            }
        }
    }
    assert(legacy::buildGrassMesh().size() == 40);
    std::vector<uint32_t> initial(960 * 266), rotated(initial.size()), wrapped(initial.size());
    renderMinecraftFrame(initial.data(), 960, 266, 0, 45, 30);
    renderMinecraftFrame(rotated.data(), 960, 266, 0, 105, 30);
    renderMinecraftFrame(wrapped.data(), 960, 266, 0, 405, 30);
    assert(initial != rotated);
    assert(initial == wrapped);
    renderMinecraftFrame(rotated.data(), 960, 266, 0, 45, -50);
    assert(initial != rotated);
    for (int yaw = 0; yaw < 360; yaw += 15) {
        for (int pitch : {-80, 0, 80}) {
            renderMinecraftFrame(rotated.data(), 960, 266, 0, float(yaw), float(pitch));
            for (const auto pixel : rotated) assert((pixel >> 24) == 255);
        }
    }
    for (int block = 0; block < legacy::terrainBlockCount(); ++block) {
        const auto& mesh = legacy::buildTerrainMesh(block);
        assert(mesh.size() % 4 == 0);
        for (const auto& vertex : mesh) {
            assert(std::isfinite(vertex.x) && std::isfinite(vertex.y) && std::isfinite(vertex.z));
            assert(vertex.u >= 0 && vertex.u <= 1 && vertex.v >= 0 && vertex.v <= 1);
        }
        renderMinecraftFrame(rotated.data(), 960, 266, 0, 45, 30, block);
        for (const auto pixel : rotated) assert((pixel >> 24) == 255);
    }
    if (argc > 2) {
        const float yaw = argc > 3 ? std::stof(argv[3]) : 105;
        const float pitch = argc > 4 ? std::stof(argv[4]) : 30;
        renderMinecraftFrame(rotated.data(), 960, 266, 0, yaw, pitch);
        std::ofstream output(argv[2], std::ios::binary);
        output << "P6\n960 266\n255\n";
        for (const auto pixel : rotated) {
            const char rgb[] = {char(pixel >> 16), char(pixel >> 8), char(pixel)};
            output.write(rgb, 3);
        }
    }
}
