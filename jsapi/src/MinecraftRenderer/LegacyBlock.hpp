#pragma once

#include <string>
#include <vector>

// Deliberately independent of the SDK and the upstream engine headers.
namespace legacy {

struct SharedConstants {
    static constexpr bool TEXTURE_LIGHTING = false;
    static constexpr int WORLD_RESOLUTION = 16;
};

enum Facing { DOWN = 0, UP = 1, NORTH = 2, SOUTH = 3, WEST = 4, EAST = 5 };

struct Icon {
    static constexpr int IS_GRASS_SIDE = 1;

    // All bounds are normalized coordinates in the same shared atlas.
    float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
    int flags = 0;

    Icon() = default;
    Icon(float u0, float v0, float u1, float v1, int flags = 0)
        : u0(u0), v0(v0), u1(u1), v1(v1), flags(flags) {}

    // The upstream bool selects terrain UVs; this adapter has only one atlas.
    float getU(float pixel, bool = true) const { return u0 + (u1 - u0) * pixel / 16.0f; }
    float getV(float pixel, bool = true) const { return v0 + (v1 - v0) * pixel / 16.0f; }
    float getU0(bool = true) const { return u0; }
    float getU1(bool = true) const { return u1; }
    float getV0(bool = true) const { return v0; }
    float getV1(bool = true) const { return v1; }
    int getFlags() const { return flags; }
};

class IconRegister {
public:
    // Implemented by LegacyBlock.cpp: return stable, atlas-normalized icons.
    Icon* registerIcon(const std::wstring& name);
};

class Material {
public:
    static Material* grass;
    static Material* snow;
    static Material* topSnow;

    explicit Material(bool liquid = false) : liquid_(liquid) {}
    bool isLiquid() const { return liquid_; }

private:
    bool liquid_ = false;
};

namespace detail {
inline Material grassMaterial;
inline Material snowMaterial;
inline Material topSnowMaterial;
} // namespace detail

inline Material* Material::grass = &detail::grassMaterial;
inline Material* Material::snow = &detail::snowMaterial;
inline Material* Material::topSnow = &detail::topSnowMaterial;

class Biome {
public:
    // Fixed plains-style grass tint; no biome/lighting SDK dependencies.
    static constexpr int GRASS_COLOR = 0x7cbd6b;
    int getGrassColor() const { return GRASS_COLOR; }
};

class LevelSource {
public:
    virtual ~LevelSource() = default;
    // The isolated block has no neighboring material (including snow above).
    virtual Material* getMaterial(int, int, int) { return nullptr; }
    virtual Biome* getBiome(int, int) { return &biome_; }
    virtual int getData(int, int, int) { return 0; }

private:
    Biome biome_;
};

class GrassTile;

class Tile {
public:
    inline static GrassTile* grass = nullptr;
    inline static Tile* dirt = nullptr;

    int id = 0;
    Material* material = Material::grass;
    Icon* icon = nullptr;

    explicit Tile(int id = 0, Material* material = Material::grass)
        : id(id), material(material) {}
    virtual ~Tile() = default;

    virtual Icon* getTexture(int face) { return getTexture(face, 0); }
    virtual Icon* getTexture(int, int) { return icon; }
    virtual Icon* getTexture(LevelSource* level, int x, int y, int z, int face)
    {
        return getTexture(face, level->getData(x, y, z));
    }
    virtual int getLightColor(LevelSource*, int, int, int) { return 0x00f000f0; }
    virtual float getBrightness(LevelSource*, int, int, int) { return 1.0f; }
    virtual bool shouldRenderFace(LevelSource*, int, int, int, int) { return true; }
    virtual int getColor() const { return 0xffffff; }
    virtual int getColor(int) { return getColor(); }
    virtual int getColor(LevelSource* level, int x, int y, int z)
    {
        return getColor(level, x, y, z, level->getData(x, y, z));
    }
    virtual int getColor(LevelSource*, int, int, int, int) { return getColor(); }
};

class GrassTile : public Tile {
public:
    Icon* iconTop = nullptr;
    Icon* iconSnowSide = nullptr;
    Icon* iconSideOverlay = nullptr;

    explicit GrassTile(int id = 2) : Tile(id, Material::grass) {}
    using Tile::getTexture;

    // These five definitions are copied verbatim into LegacyTileRenderer.inc.
    Icon* getTexture(int face, int data) override;
    Icon* getTexture(LevelSource* level, int x, int y, int z, int face) override;
    void registerIcons(IconRegister* iconRegister);
    int getColor(LevelSource* level, int x, int y, int z, int data) override;
    static Icon* getSideTextureOverlay();

    int getColor() const override { return Biome::GRASS_COLOR; }
    int getColor(int) override { return getColor(); }
    int getColor(LevelSource* level, int x, int y, int z) override
    {
        return getColor(level, x, y, z, level->getData(x, y, z));
    }
};

struct Vertex {
    float x, y, z, u, v, r, g, b;
    float a = 1.0f;
};

class Tesselator {
public:
    std::vector<Vertex> vertices;

    static Tesselator* getInstance()
    {
        static Tesselator instance;
        return &instance;
    }
    void color(float r, float g, float b) { r_ = r; g_ = g; b_ = b; }
    void color(int color) {
        this->color(((color >> 16) & 255) / 255.0f,
                    ((color >> 8) & 255) / 255.0f, (color & 255) / 255.0f);
    }
    void addOffset(float x, float y, float z) { offsetX_ += x; offsetY_ += y; offsetZ_ += z; }
    void tex2(int) {} // Full-bright path has no secondary lightmap.
    void vertexUV(float x, float y, float z, float u, float v)
    {
        vertices.push_back({x + offsetX_, y + offsetY_, z + offsetZ_, u, v, r_, g_, b_});
    }

private:
    float r_ = 1.0f, g_ = 1.0f, b_ = 1.0f;
    float offsetX_ = 0, offsetY_ = 0, offsetZ_ = 0;
};

class TileRenderer {
public:
    enum { FLIP_NONE = 0, FLIP_CW = 1, FLIP_CCW = 2, FLIP_180 = 3 };
    inline static bool fancy = true;

    LevelSource* level = nullptr;
    Icon* fixedTexture = nullptr;
    bool applyAmbienceOcclusion = false;
    bool noCulling = false;
    bool xFlipTexture = false;
    double tileShapeX0 = 0.0, tileShapeY0 = 0.0, tileShapeZ0 = 0.0;
    double tileShapeX1 = 1.0, tileShapeY1 = 1.0, tileShapeZ1 = 1.0;
    int downFlip = FLIP_NONE, upFlip = FLIP_NONE;
    int northFlip = FLIP_NONE, southFlip = FLIP_NONE;
    int westFlip = FLIP_NONE, eastFlip = FLIP_NONE;
    float c1r = 1.0f, c1g = 1.0f, c1b = 1.0f;
    float c2r = 1.0f, c2g = 1.0f, c2b = 1.0f;
    float c3r = 1.0f, c3g = 1.0f, c3b = 1.0f;
    float c4r = 1.0f, c4g = 1.0f, c4b = 1.0f;
    int tc1 = 0, tc2 = 0, tc3 = 0, tc4 = 0;

    explicit TileRenderer(LevelSource* level = nullptr) : level(level) {}
    bool hasFixedTexture() const { return fixedTexture != nullptr; }
    Icon* getTexture(Tile* tile, LevelSource* source, int x, int y, int z, int face)
    {
        return tile->getTexture(source, x, y, z, face);
    }
    Icon* getTexture(Tile* tile, int face, int data) { return tile->getTexture(face, data); }
    Icon* getTexture(Tile* tile, int face) { return tile->getTexture(face); }
    Icon* getTexture(Tile* tile) { return tile->getTexture(Facing::UP); }
    int getLightColor(Tile*, LevelSource*, int, int, int) const { return 0x00f000f0; }

    void tesselateCrossTexture(Tile* tt, int data, float x, float y, float z, float scale);
    void tesselateRowTexture(Tile* tt, int data, float x, float y, float z);
    bool tesselateCactusInWorld(Tile* tt, int x, int y, int z, float r, float g, float b);
    bool tesselateLilypadInWorld(Tile* tt, int x, int y, int z);
    bool tesselateBlockInWorld(Tile* tt, int x, int y, int z, float r, float g, float b);
    void renderFaceDown(Tile* tt, double x, double y, double z, Icon* tex);
    void renderFaceUp(Tile* tt, double x, double y, double z, Icon* tex);
    // Preserve the four original upstream names (there is no 'Face' prefix).
    void renderNorth(Tile* tt, double x, double y, double z, Icon* tex);
    void renderSouth(Tile* tt, double x, double y, double z, Icon* tex);
    void renderWest(Tile* tt, double x, double y, double z, Icon* tex);
    void renderEast(Tile* tt, double x, double y, double z, Icon* tex);
};

// Implemented by LegacyBlock.cpp. Each consecutive four vertices form one quad.
std::vector<Vertex> buildGrassMesh();
const std::vector<Vertex>& buildTerrainMesh(int blockIndex);
int terrainBlockCount();

} // namespace legacy
