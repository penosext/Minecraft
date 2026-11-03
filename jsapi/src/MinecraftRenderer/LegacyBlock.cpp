#include "LegacyBlock.hpp"
#include "LegacyCatalog.hpp"
#include "SpecialShapes.hpp"
#include "WorldRenderer.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <string>

#include "LegacyTileRenderer.inc"

namespace legacy {
Icon* IconRegister::registerIcon(const std::wstring& name)
{
    static std::map<std::wstring, Icon> icons = [] {
        std::map<std::wstring, Icon> result;
        for (const auto& spec : kIconSpecs)
            result.emplace(spec.name, Icon(spec.u0, spec.v0, spec.u1, spec.v1, spec.flags));
        return result;
    }();
    const auto found = icons.find(name);
    if (found == icons.end()) throw std::runtime_error("Texture not embedded in terrain catalog");
    return &found->second;
}

namespace {
class PreviewTile : public Tile {
public:
    Icon* faces[6] = {};
    int tint = 0xffffff;
    explicit PreviewTile(const BlockSpec& spec, IconRegister& registry) : Tile(1), tint(spec.color) {
        for (int face = 0; face < 6; ++face) faces[face] = registry.registerIcon(spec.faces[face]);
        icon = faces[0];
    }
    Icon* getTexture(int face, int) override { return faces[std::clamp(face, 0, 5)]; }
    Icon* getTexture(LevelSource*, int, int, int, int face) override { return getTexture(face, 0); }
    int getColor() const override { return tint; }
};

void box(TileRenderer& renderer, Tile* tile, float r, float g, float b,
         float x0, float y0, float z0, float x1, float y1, float z1)
{
    renderer.tileShapeX0 = x0; renderer.tileShapeY0 = y0; renderer.tileShapeZ0 = z0;
    renderer.tileShapeX1 = x1; renderer.tileShapeY1 = y1; renderer.tileShapeZ1 = z1;
    renderer.tesselateBlockInWorld(tile, 0, 0, 0, r, g, b);
}

std::vector<Vertex> generateMesh(const BlockSpec& spec)
{
    static IconRegister registry;
    static Tile dirt(3);
    static GrassTile grass(2);
    static const bool initialized = [] {
        dirt.icon = registry.registerIcon(L"dirt");
        grass.registerIcons(&registry);
        Tile::dirt = &dirt;
        Tile::grass = &grass;
        return true;
    }();
    (void)initialized;
    LevelSource level;
    TileRenderer renderer(&level);
    Tesselator* tesselator = Tesselator::getInstance();
    tesselator->vertices.clear();
    PreviewTile preview(spec, registry);
    Tile* tile = std::string(spec.shape) == "grass" ? static_cast<Tile*>(&grass) : &preview;
    const bool snowyGrass = std::string(spec.key) == "grass_snowy";
    const int color = std::string(spec.shape) == "grass" ? grass.getColor(&level, 0, 0, 0)
                                                     : (snowyGrass ? 0xffffff : int(spec.color));
    const float r = ((color >> 16) & 255) / 255.0f;
    const float g = ((color >> 8) & 255) / 255.0f;
    const float b = (color & 255) / 255.0f;
    const std::string shape = spec.shape;
    if (shape == "air") return {};
    // Shared cell-local geometry, not the complete original renderer. Upper
    // door metadata alone has no lower direction: isolated preview falls back
    // to direction 0; world rendering combines the adjacent halves instead.
    if (shape == "stairs" || shape == "slab" || shape == "door" || shape == "pane" ||
        shape == "fence" || shape == "wall" || shape == "torch" || shape == "ladder" ||
        shape == "rail") return worldrender::buildShapeMesh(spec);
    if (renderSpecialShape(renderer, tile, spec, r, g, b)) {
    } else if (shape == "crops") {
        tesselator->color(r, g, b);
        renderer.tesselateRowTexture(tile, spec.data, 0, 0, 0);
    } else if (shape == "cross" || shape == "plant" || shape == "torch") {
        tesselator->color(r, g, b);
        renderer.tesselateCrossTexture(tile, 0, 0, 0, 0, shape == "torch" ? 0.65f : 1.0f);
    } else if (shape == "cactus") {
        renderer.tesselateCactusInWorld(tile, 0, 0, 0, r, g, b);
    } else if (shape == "lilypad") {
        renderer.tesselateLilypadInWorld(tile, 0, 0, 0);
    } else if (shape == "vine") {
        tesselator->color(r, g, b);
        renderer.tileShapeZ0 = 0.98f;
        renderer.renderNorth(tile, 0, 0, 0, preview.faces[2]);
    } else if (shape == "rail" || shape == "wire" || shape == "tripwire") {
        tesselator->color(r, g, b);
        renderer.tileShapeY1 = 0.0625f;
        renderer.renderFaceUp(tile, 0, 0, 0, preview.faces[1]);
    } else {
        float height = spec.height;
        if (shape == "liquid") height = 0.875f;
        if (shape == "slab") height = 0.5f;
        const float bottom = shape == "slab" && (spec.data & 8) ? 0.5f : 0.0f;
        box(renderer, tile, r, g, b, 0, bottom, 0, 1, bottom + height, 1);
    }
    auto vertices = tesselator->vertices;
    if (snowyGrass && vertices.size() >= 8) {
        for (size_t i = 4; i < 8; ++i) {
            vertices[i].r *= ((spec.color >> 16) & 255) / 255.0f;
            vertices[i].g *= ((spec.color >> 8) & 255) / 255.0f;
            vertices[i].b *= (spec.color & 255) / 255.0f;
        }
    }
    const bool water = std::string(spec.key) == "water" || std::string(spec.key) == "calmWater";
    for (auto& vertex : vertices) vertex.a = water ? 0.55f : spec.alpha;
    return vertices;
}
}

int terrainBlockCount() { return kBlockCount; }

const std::vector<Vertex>& buildTerrainMesh(int blockIndex)
{
    static std::map<int, std::vector<Vertex>> cache;
    blockIndex = std::clamp(blockIndex, 0, kBlockCount - 1);
    auto found = cache.find(blockIndex);
    if (found == cache.end()) found = cache.emplace(blockIndex, generateMesh(kBlockSpecs[blockIndex])).first;
    return found->second;
}

std::vector<Vertex> buildGrassMesh()
{
    static const int index = [] {
        for (int i = 0; i < kBlockCount; ++i)
            if (std::string(kBlockSpecs[i].shape) == "grass") return i;
        return 0;
    }();
    return buildTerrainMesh(index);
}
}
