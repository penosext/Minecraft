#include "SpecialShapes.hpp"
#include "LegacyBlock.hpp"
#include "LegacyCatalog.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

namespace legacy {
namespace {
constexpr float P = 1.0f / 16.0f;
constexpr float radians = 3.14159265358979323846f / 180.0f;
struct Point { float x, y, z; };

// These are adaptations, NOT verbatim extractions. Source lineage is the pinned
// minecraft-console tree (09df8928eeb15328bca499d225e48ea95af6d119):
// port/upstream/Minecraft.Client/TileRenderer.cpp:
//   end frame 447-484; cauldron 805-852; flower pot 858-980;
//   repeater 1185-1292, diode 1372-1445, torch 4247-4314;
//   piston base 1455-1560 and extension 1638-1768;
//   lever 1772-1969; tripwire hook 1973-2313; fire 2456-2554;
//   cocoa 4997-5197. Face methods and cross quads use the existing upstream port.
// Chest dimensions additionally come from the local original
// /root/1234/ref/mcpartial/minecraft-console-main/Minecraft.Client/ChestModel.cpp:7-23.
// Pressure plate/button tile shape sources are NOT present in port/upstream;
// their inset/thickness below is a conventional isolated legacy approximation.
// No neighboring blocks, animation, tile entities, locked repeaters, connected
// chests, or moving piston assemblies are simulated here.

struct RestoreRenderer {
    TileRenderer& renderer;
    TileRenderer saved;
    explicit RestoreRenderer(TileRenderer& value) : renderer(value), saved(value) {}
    ~RestoreRenderer() { renderer = saved; }
};

Icon* icon(const wchar_t* name) { return IconRegister().registerIcon(name); }

Point xRot(Point p, float degrees)
{
    const float c = std::cos(degrees * radians), s = std::sin(degrees * radians);
    return {p.x, p.y * c + p.z * s, p.z * c - p.y * s};
}
Point yRot(Point p, float degrees)
{
    const float c = std::cos(degrees * radians), s = std::sin(degrees * radians);
    return {p.x * c + p.z * s, p.y, p.z * c - p.x * s};
}

void quad(Icon* tex, Point a, Point b, Point c, Point d,
          float u0 = 0, float v0 = 0, float u1 = 16, float v1 = 16,
          bool doubleSided = false)
{
    auto* t = Tesselator::getInstance();
    const auto vertex = [&](Point p, float u, float v) {
        t->vertexUV(p.x, p.y, p.z, tex->getU(u), tex->getV(v));
    };
    vertex(a, u0, v1); vertex(b, u1, v1); vertex(c, u1, v0); vertex(d, u0, v0);
    if (doubleSided) {
        vertex(d, u0, v0); vertex(c, u1, v0); vertex(b, u1, v1); vertex(a, u0, v1);
    }
}

struct Geometry {
    TileRenderer& renderer;
    Tile* tile;
    float r, g, b;
    int data;

    void color(float shade = 1) const { Tesselator::getInstance()->color(r * shade, g * shade, b * shade); }
    Icon* texture(int face) const {
        return renderer.fixedTexture ? renderer.fixedTexture : tile->getTexture(face, data);
    }
    void shape(float x0, float y0, float z0, float x1, float y1, float z1) const {
        renderer.tileShapeX0 = x0; renderer.tileShapeY0 = y0; renderer.tileShapeZ0 = z0;
        renderer.tileShapeX1 = x1; renderer.tileShapeY1 = y1; renderer.tileShapeZ1 = z1;
    }
    void box(float x0, float y0, float z0, float x1, float y1, float z1, Icon* fixed = nullptr) const {
        shape(x0, y0, z0, x1, y1, z1);
        const auto tex = [&](int face) { return fixed ? fixed : texture(face); };
        color(0.5f); renderer.renderFaceDown(tile, 0, 0, 0, tex(DOWN));
        color(); renderer.renderFaceUp(tile, 0, 0, 0, tex(UP));
        color(0.8f); renderer.renderNorth(tile, 0, 0, 0, tex(NORTH));
        renderer.renderSouth(tile, 0, 0, 0, tex(SOUTH));
        color(0.6f); renderer.renderWest(tile, 0, 0, 0, tex(WEST));
        renderer.renderEast(tile, 0, 0, 0, tex(EAST));
    }
    void top(float x0, float y, float z0, float x1, float z1, Icon* tex) const {
        shape(x0, y, z0, x1, y, z1);
        color(); renderer.renderFaceUp(tile, 0, 0, 0, tex);
    }
};

// The same corner topology as the original lever/hook, with cropped UVs to
// avoid sampling transparent areas of those item-like terrain sprites.
template <typename Transform>
void rod(const Geometry& g, Icon* tex, float halfX, float height, float halfZ,
         Transform transform, float u0, float v0, float u1, float v1)
{
    std::array<Point, 8> points = {{{-halfX, 0, -halfZ}, {halfX, 0, -halfZ},
        {halfX, 0, halfZ}, {-halfX, 0, halfZ}, {-halfX, height, -halfZ},
        {halfX, height, -halfZ}, {halfX, height, halfZ}, {-halfX, height, halfZ}}};
    for (auto& p : points) p = transform(p);
    constexpr int faces[6][4] = {{0, 1, 2, 3}, {7, 6, 5, 4}, {1, 0, 4, 5},
                                {2, 1, 5, 6}, {3, 2, 6, 7}, {0, 3, 7, 4}};
    for (int face = 0; face < 6; ++face) {
        g.color(face == 0 ? 0.5f : (face == 1 ? 1.0f : 0.8f));
        const auto& f = faces[face];
        quad(tex, points[f[0]], points[f[1]], points[f[2]], points[f[3]],
             u0, v0, u1, face < 2 ? v0 + 2 : v1);
    }
}

void chest(const Geometry& g)
{
    // Closed, single chest only. Projected 16x16 chest_* icons are supplied by
    // the catalog; splitting at y=10 preserves the original 14x10x14 / 14x5x14.
    g.box(P, 0, P, 15 * P, 10 * P, 15 * P);
    g.box(P, 10 * P, P, 15 * P, 15 * P, 15 * P);
    // Original lock is 2x4x1. Use the central front projection for its metal
    // patch rather than introducing an invented terrain material.
    Icon* front = g.texture(NORTH);
    g.box(7 * P, 8 * P, 0, 9 * P, 12 * P, P, front);
}

void cocoa(const Geometry& g)
{
    // Original pod dimensions and UV regions, including the age-2 top special
    // case and double-sided stalk. Direction enum: S=0,W=1,N=2,E=3.
    const int age = std::clamp(g.data >> 2, 0, 2), dir = g.data & 3;
    const float width = float(4 + age * 2), height = float(5 + age * 2);
    float offX = 8 - width / 2, offZ = 15 - width;
    if (dir == 2) offZ = 1;
    if (dir == 1) { offX = 1; offZ = 8 - width / 2; }
    if (dir == 3) { offX = 15 - width; offZ = 8 - width / 2; }
    const float x0 = offX * P, x1 = (offX + width) * P;
    const float z0 = offZ * P, z1 = (offZ + width) * P;
    const float y0 = (12 - height) * P, y1 = 12 * P;
    Icon* tex = g.texture(UP);
    g.color(0.8f);
    quad(tex, {x0,y0,z0}, {x0,y0,z1}, {x0,y1,z1}, {x0,y1,z0}, 15-width,4,15,4+height);
    quad(tex, {x1,y0,z1}, {x1,y0,z0}, {x1,y1,z0}, {x1,y1,z1}, 15-width,4,15,4+height);
    quad(tex, {x1,y0,z0}, {x0,y0,z0}, {x0,y1,z0}, {x1,y1,z0}, 15-width,4,15,4+height);
    quad(tex, {x0,y0,z1}, {x1,y0,z1}, {x1,y1,z1}, {x0,y1,z1}, 15-width,4,15,4+height);
    const float topWidth = width - (age == 2 ? 1 : 0);
    g.color(); quad(tex, {x0,y1,z1}, {x1,y1,z1}, {x1,y1,z0}, {x0,y1,z0}, 0,0,topWidth,topWidth);
    g.color(0.5f); quad(tex, {x1,y0,z1}, {x0,y0,z1}, {x0,y0,z0}, {x1,y0,z0}, 0,0,topWidth,topWidth);
    g.color();
    if (dir == 0 || dir == 2) {
        const float z = dir == 2 ? 0 : 12 * P;
        quad(tex, {0.5f,12*P,z}, {0.5f,12*P,z+4*P},
             {0.5f,1,z+4*P}, {0.5f,1,z}, 12,0,16,4,true);
    } else {
        const float x = dir == 1 ? 0 : 12 * P;
        quad(tex, {x,12*P,0.5f}, {x+4*P,12*P,0.5f},
             {x+4*P,1,0.5f}, {x,1,0.5f}, 12,0,16,4,true);
    }
}

void button(const Geometry& g)
{
    const float depth = (g.data & 8) ? P : 2 * P;
    switch (g.data & 7) {
    case 1: g.box(0, 6*P, 5*P, depth, 10*P, 11*P); break;
    case 2: g.box(1-depth, 6*P, 5*P, 1, 10*P, 11*P); break;
    case 3: g.box(5*P, 6*P, 0, 11*P, 10*P, depth); break;
    default: g.box(5*P, 6*P, 1-depth, 11*P, 10*P, 1); break;
    }
}

void lever(const Geometry& g)
{
    const int dir = g.data & 7;
    Icon* base = g.texture(DOWN);
    switch (dir) {
    case 5: g.box(5*P,0,4*P,11*P,3*P,12*P,base); break;
    case 6: g.box(4*P,0,5*P,12*P,3*P,11*P,base); break;
    case 4: g.box(5*P,4*P,13*P,11*P,12*P,1,base); break;
    case 3: g.box(5*P,4*P,0,11*P,12*P,3*P,base); break;
    case 2: g.box(13*P,4*P,5*P,1,12*P,11*P,base); break;
    case 1: g.box(0,4*P,5*P,3*P,12*P,11*P,base); break;
    case 0: g.box(4*P,13*P,5*P,12*P,1,11*P,base); break;
    default: g.box(5*P,13*P,4*P,11*P,1,12*P,base); break;
    }
    rod(g, g.texture(UP), P, 10*P, P, [&](Point p) {
        const bool flipped = (g.data & 8) != 0;
        p.z += flipped ? -P : P;
        p = xRot(p, flipped ? 40 : -40);
        if (dir == 0 || dir == 7) { p.x = -p.x; p.y = -p.y; }
        if (dir == 6 || dir == 0) p = yRot(p, 90);
        if (dir > 0 && dir < 5) {
            p.y -= 6*P; p = xRot(p,90);
            p = yRot(p, dir == 3 ? 180 : (dir == 2 ? 90 : (dir == 1 ? -90 : 0)));
            p.y += 8*P;
        } else p.y += (dir == 0 || dir == 7) ? 14*P : 2*P;
        p.x += 0.5f; p.z += 0.5f;
        return p;
    }, 7,6,9,16);
}

void repeater(const Geometry& g)
{
    // Static unlocked preview. The original floor is y=2/16 and torch cap
    // y=(-3+10)/16. Delay movement is approximated by four pixel-spaced slots;
    // DELAY_RENDER_OFFSETS' definition is not in the vendored source subset.
    g.box(0,0,0,1,2*P,1);
    Icon* torch = icon(L"redstone_torch_off");
    const int dir = g.data & 3;
    const float transmitter = float(1 + ((g.data >> 2) & 3) * 2) * P;
    for (float offset : {-5*P, transmitter}) {
        float x = 0.5f, z = 0.5f;
        if (dir == 0) z += offset;
        if (dir == 2) z -= offset;
        if (dir == 3) x += offset;
        if (dir == 1) x -= offset;
        // Cropped 2-pixel wide solid torch, rather than a repeater-textured post.
        rod(g, torch, P, 5*P, P, [=](Point p) {
            return Point{p.x+x, p.y+2*P, p.z+z};
        }, 7,6,9,11);
    }
}

void piston(const Geometry& g)
{
    // Catalog faces describe a canonical UP-facing retracted piston. The
    // original retracted base IS a complete cube; it must not grow a fake arm.
    // For the extended bit show only the shortened base, as the source does;
    // the adjacent extension/moving-block entity is deliberately not invented.
    if (!(g.data & 8)) { g.box(0,0,0,1,1,1); return; }
    constexpr float thickness = 4*P; // conventional platform; header not vendored
    float x0=0,y0=0,z0=0,x1=1,y1=1,z1=1;
    switch (g.data & 7) {
    case DOWN: y0=thickness; break;
    case NORTH: z0=thickness; break;
    case SOUTH: z1-=thickness; break;
    case WEST: x0=thickness; break;
    case EAST: x1-=thickness; break;
    default: y1-=thickness; break;
    }
    g.box(x0,y0,z0,x1,y1,z1);
}

void tripwireHook(const Geometry& g)
{
    // Original 4x8x2 mounting block (center y=.3), 1.5x5x1.5 stick,
    // and 3x.5x3 hoop envelope. The hoop below is genuinely open, formed by
    // four small boxes, rather than a solid sprite slab. No connected wire.
    const int dir = g.data & 3;
    Icon* wood = icon(L"planks_oak");
    switch (dir) {
    case 2: g.box(6*P,0.05f,14*P,10*P,0.55f,1,wood); break;
    case 0: g.box(6*P,0.05f,0,10*P,0.55f,2*P,wood); break;
    case 1: g.box(14*P,0.05f,6*P,1,0.55f,10*P,wood); break;
    default:g.box(0,0.05f,6*P,2*P,0.55f,10*P,wood); break;
    }
    const float turn = dir == 0 ? 180 : (dir == 1 ? 90 : (dir == 3 ? -90 : 0));
    const bool powered = (g.data & 8) != 0, attached = (g.data & 4) != 0;
    rod(g, g.texture(DOWN), 0.75f*P, 5*P, 0.75f*P, [&](Point p) {
        p.z += P;
        p = xRot(p, powered ? 30 : (attached ? 5 : -40));
        p.y -= (powered || attached) ? 7*P : 6*P;
        p = yRot(xRot(p,90),turn);
        p.x += 0.5f; p.y += 5*P; p.z += 0.5f;
        return p;
    },7,9,9,16);
    const auto hoopTransform = [&](Point p) {
        p.z += 3.5f*P;
        if (powered) { p.y -= 1.5f*P; p.z -= 2.6f*P; }
        else if (attached) { p.y += 0.25f*P; p.z -= 2.75f*P; p=xRot(p,10); }
        else p=xRot(p,50);
        p=yRot(p,turn);
        return Point{p.x+0.5f,p.y+5*P,p.z+0.5f};
    };
    // Transform each bar with the same original hoop pivot.
    for (int side=0; side<4; ++side) {
        const bool alongX=side<2;
        const float center = side%2 == 0 ? -1.25f*P : 1.25f*P;
        rod(g,g.texture(DOWN),alongX ? 1.5f*P : 0.25f*P,0.5f*P,
            alongX ? 0.25f*P : P,[&](Point p) {
                if (alongX) p.z+=center; else p.x+=center;
                return hoopTransform(p);
            },5,0,6,1);
    }
}

void fire(const Geometry& g)
{
    // Ground-supported branch of tesselateFireInWorld, frozen animation frame.
    // Four inner tongues lean out, four outer tongues lean in; h is the source
    // 1.4, not a cube. Ignore all world-neighbor attachment predicates.
    Icon* first=g.texture(UP), *second=icon(L"fire_1");
    g.color();
    quad(first,{0.7f,0,0},{0.7f,0,1},{0.2f,1.4f,1},{0.2f,1.4f,0},0,0,16,16,true);
    quad(first,{0.3f,0,1},{0.3f,0,0},{0.8f,1.4f,0},{0.8f,1.4f,1},0,0,16,16,true);
    quad(second,{0,0,0.3f},{1,0,0.3f},{1,1.4f,0.8f},{0,1.4f,0.8f},0,0,16,16,true);
    quad(second,{1,0,0.7f},{0,0,0.7f},{0,1.4f,0.2f},{1,1.4f,0.2f},0,0,16,16,true);
    quad(second,{0,0,1},{0,0,0},{0.1f,1.4f,0},{0.1f,1.4f,1},0,0,16,16,true);
    quad(second,{1,0,0},{1,0,1},{0.9f,1.4f,1},{0.9f,1.4f,0},0,0,16,16,true);
    quad(first,{0,0,0},{1,0,0},{1,1.4f,0.1f},{0,1.4f,0.1f},0,0,16,16,true);
    quad(first,{1,0,1},{0,0,1},{0,1.4f,0.9f},{1,1.4f,0.9f},0,0,16,16,true);
}

void cauldron(const Geometry& g)
{
    // Source has full-height alpha-cutout outer faces plus an inside at 2/16,
    // floor y=4/16. Explicit wall/leg geometry keeps the opening visible even
    // without relying on alpha in the cauldron_top sprite.
    g.box(0,3*P,0,1,4*P,1);
    g.box(0,4*P,0,2*P,1,1);
    g.box(14*P,4*P,0,1,1,1);
    g.box(2*P,4*P,0,14*P,1,2*P);
    g.box(2*P,4*P,14*P,14*P,1,1);
    for (float x : {0.0f,12*P}) for (float z : {0.0f,12*P})
        g.box(x,0,z,x+4*P,3*P,z+4*P);
    g.top(2*P,4*P+0.0001f,2*P,14*P,14*P,icon(L"cauldron_inner"));
    // Empty isolated preview: no invented water level or animated contents.
}

void flowerPot(const Geometry& g)
{
    // FlowerPotTile.cpp:14-18 is a 6x6x6 outer envelope. Wall thickness is
    // approximated as 1 pixel; the source's dirt surface is approximately y=5.
    Icon* pot=g.texture(DOWN);
    g.box(5*P,0,5*P,11*P,P,11*P,pot);
    g.box(5*P,P,5*P,6*P,6*P,11*P,pot);
    g.box(10*P,P,5*P,11*P,6*P,11*P,pot);
    g.box(6*P,P,5*P,10*P,6*P,6*P,pot);
    g.box(6*P,P,10*P,10*P,6*P,11*P,pot);
    g.top(6*P,5*P,6*P,10*P,10*P,icon(L"dirt"));
    // Catalog entry is the swamp-hut red mushroom. This intentionally does not
    // interpret arbitrary flower-pot data/contents or emulate a tile entity.
    Tile mushroom;
    mushroom.icon=icon(L"mushroom_red");
    g.color();
    g.renderer.tesselateCrossTexture(&mushroom,0,0,4*P,0,0.75f);
}

void endFrame(const Geometry& g)
{
    g.box(0,0,0,1,13*P,1);
    if (g.data & 4) {
        Icon* eye=icon(L"endframe_eye");
        // Source uses a 4..12/16,13..16/16 cuboid. Cropping the full eye
        // sprite to that cuboid avoids the plain block face UV projection.
        rod(g,eye,4*P,3*P,4*P,[](Point p) {
            return Point{p.x+0.5f,p.y+13*P,p.z+0.5f};
        },4,0,12,3);
        g.color();
        quad(eye,{4*P,1,12*P},{12*P,1,12*P},{12*P,1,4*P},{4*P,1,4*P},4,4,12,12);
    }
}
} // namespace

bool renderSpecialShape(TileRenderer& renderer, Tile* tile, const BlockSpec& spec,
                        float r, float g, float b)
{
    const std::string_view shape = spec.shape ? spec.shape : "";
    using Render = void (*)(const Geometry&);
    Render render = nullptr;
    if (shape == "chest") render=chest;
    else if (shape == "cocoa") render=cocoa;
    else if (shape == "button") render=button;
    else if (shape == "lever") render=lever;
    else if (shape == "repeater") render=repeater;
    else if (shape == "piston") render=piston;
    else if (shape == "tripwire_hook") render=tripwireHook;
    else if (shape == "fire") render=fire;
    else if (shape == "cauldron") render=cauldron;
    else if (shape == "flower_pot") render=flowerPot;
    else if (shape == "endframe") render=endFrame;
    else if (shape != "pressure_plate") return false;
    if (!tile) return false;

    RestoreRenderer restore(renderer);
    renderer.applyAmbienceOcclusion=false;
    renderer.noCulling=true;
    renderer.xFlipTexture=false;
    renderer.downFlip=renderer.upFlip=renderer.northFlip=renderer.southFlip=
        renderer.westFlip=renderer.eastFlip=TileRenderer::FLIP_NONE;
    const Geometry geometry{renderer,tile,r,g,b,spec.data};
    if (render) render(geometry);
    else geometry.box(P,0,P,15*P,P,15*P); // unpressed, inset 14x14x1 plate
    return true;
}
} // namespace legacy
