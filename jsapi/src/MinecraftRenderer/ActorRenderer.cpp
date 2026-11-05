#include "ActorRenderer.hpp"
#include "SteveSkin.hpp"
#include "OriginalFirstPerson.hpp"
#include "LegacyCatalog.hpp"
#include "LegacyTextures.hpp"
#include "ItemIcons.hpp"
#include "ItemCatalog.hpp"
#include "WorldRenderer.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {
constexpr float kPi = 3.14159265358979323846f;
struct Vec3 { float x, y, z; };
struct Vec4 { float x, y, z, w; };
struct Matrix {
    // Row-major storage, column vectors, OpenGL M <- M*T/R/S.
    std::array<float, 16> m{{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}};
    void multiply(const Matrix& b) {
        Matrix result;
        result.m.fill(0);
        for (int r=0;r<4;++r) for (int c=0;c<4;++c) for (int k=0;k<4;++k)
            result.m[r*4+c] += m[r*4+k]*b.m[k*4+c];
        *this=result;
    }
    Vec4 apply(Vec4 v) const {
        return {m[0]*v.x+m[1]*v.y+m[2]*v.z+m[3]*v.w,
                m[4]*v.x+m[5]*v.y+m[6]*v.z+m[7]*v.w,
                m[8]*v.x+m[9]*v.y+m[10]*v.z+m[11]*v.w,
                m[12]*v.x+m[13]*v.y+m[14]*v.z+m[15]*v.w};
    }
    void translate(float x,float y,float z) { Matrix b; b.m[3]=x;b.m[7]=y;b.m[11]=z;multiply(b); }
    void scale(float x,float y,float z) { Matrix b;b.m[0]=x;b.m[5]=y;b.m[10]=z;multiply(b); }
    void rotate(float degrees,float x,float y,float z) {
        const float a=degrees*(kPi/180), c=std::cos(a), s=std::sin(a), t=1-c;
        Matrix b;
        b.m={{t*x*x+c,t*x*y-s*z,t*x*z+s*y,0,
              t*x*y+s*z,t*y*y+c,t*y*z-s*x,0,
              t*x*z-s*y,t*y*z+s*x,t*z*z+c,0, 0,0,0,1}};
        multiply(b); // generated calls use unit coordinate axes
    }
};
#include "OriginalFirstPerson.inc"
struct Vertex { float x, y, q, uq, vq; }; // reciprocal depth and UV/depth
struct Triangle {
    Vertex a, b, c;
    unsigned light;
    int u0, v0, u1, v1; // inclusive face texel limits, prevent atlas bleeding
    const uint32_t* texture = steve::kSkinPixels;
    int textureWidth = steve::kSkinWidth;
    uint32_t tint = 0xffffff;
};
struct Rotation {
    float cx, sx, cy, sy, cz, sz;
    Rotation(float x, float y, float z)
        : cx(std::cos(x)), sx(std::sin(x)), cy(std::cos(y)), sy(std::sin(y)),
          cz(std::cos(z)), sz(std::sin(z)) {}
    Vec3 apply(Vec3 v) const
    {
        v = {v.x, cx * v.y - sx * v.z, sx * v.y + cx * v.z};
        v = {cy * v.x + sy * v.z, v.y, -sy * v.x + cy * v.z};
        return {cz * v.x - sz * v.y, sz * v.x + cz * v.y, v.z};
    }
};
struct Projection {
    Rotation rotation;
    float centerY, distance, focal, screenX, screenY;
    Vec3 camera(Vec3 v) const
    {
        v.y -= centerY;
        v = rotation.apply(v);
        v.z += distance;
        return v;
    }
    Vertex project(Vec3 v, float u, float t) const
    {
        const float q = 1.0f / v.z;
        return {screenX + focal * v.x * q, screenY + focal * v.y * q,
                q, u * q, t * q};
    }
};
float edge(const Vertex& a, const Vertex& b, float x, float y)
{
    return (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x);
}
Vec3 cross(Vec3 a, Vec3 b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}
Vec3 subtract(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }

struct Mesh {
    // Keep the small stack buffer for hands/previews. Sprite rendering supplies
    // a separate reusable buffer: never truncate a 16x16 extruded item at 192.
    std::array<Triangle, 192> localTriangles{};
    Triangle* triangles = localTriangles.data();
    size_t capacity = localTriangles.size(), count = 0;
    Mesh() = default;
    Mesh(Triangle* storage, size_t size) : triangles(storage), capacity(size) {}
    void face(const std::array<Vec3, 8>& points, const int* indices,
              float u0, float v0, float u1, float v1, bool mirror,
              const Projection& projection)
    {
        std::array<Vec3, 4> p{};
        for (int i = 0; i < 4; ++i) p[i] = points[indices[i]];
        Vec3 normal = cross(subtract(p[1], p[0]), subtract(p[2], p[0]));
        if (mirror) normal = {-normal.x, -normal.y, -normal.z};
        // Outward normals point away from the eye for hidden faces. All model
        // vertices are safely in front of the purpose-built preview near plane.
        if (normal.x * p[0].x + normal.y * p[0].y + normal.z * p[0].z >= 0) return;
        for (const Vec3& v : p) if (v.z < 1.0f) return;
        const float invLength = 1.0f / std::sqrt(normal.x * normal.x +
                                               normal.y * normal.y + normal.z * normal.z);
        const float diffuse = std::max(0.0f, (-0.32f * normal.x - 0.56f * normal.y -
                                             0.76f * normal.z) * invLength);
        const unsigned light = unsigned(192.0f + 63.0f * diffuse);
        // Preview-only legacy rectangle layout (existing platform mesh).
        // Original view0 uses the verified Polygon signed inset below instead.
        std::array<Vertex, 4> v = {{projection.project(p[0], u1, v0),
                                  projection.project(p[1], u0, v0),
                                  projection.project(p[2], u0, v1),
                                  projection.project(p[3], u1, v1)}};
        const int left = int(std::min(u0, u1)), right = int(std::max(u0, u1)) - 1;
        const int top = int(std::min(v0, v1)), bottom = int(std::max(v0, v1)) - 1;
        triangles[count++] = {v[0], v[1], v[2], light, left, top, right, bottom};
        triangles[count++] = {v[0], v[2], v[3], light, left, top, right, bottom};
    }
    void cube(Vec3 origin, Vec3 size, Vec3 pivot, const Rotation& local,
              int u, int v, bool mirror, const Projection& projection)
    {
        // Same u0,u1,u2,u3,l0,l1,l2,l3 vertex layout as Cube.cpp 57-74.
        float x0 = origin.x, x1 = origin.x + size.x;
        if (mirror) std::swap(x0, x1);
        const float y0 = origin.y, y1 = origin.y + size.y;
        const float z0 = origin.z, z1 = origin.z + size.z;
        std::array<Vec3, 8> points = {{{x0, y0, z0}, {x1, y0, z0},
                                     {x1, y1, z0}, {x0, y1, z0},
                                     {x0, y0, z1}, {x1, y0, z1},
                                     {x1, y1, z1}, {x0, y1, z1}}};
        for (Vec3& p : points) {
            p = local.apply(p);
            p = projection.camera({p.x + pivot.x, p.y + pivot.y, p.z + pivot.z});
        }
        static constexpr int faces[6][4] = {{5, 1, 2, 6}, {0, 4, 7, 3},
                                           {5, 4, 0, 1}, {2, 3, 7, 6},
                                           {1, 0, 3, 2}, {4, 5, 6, 7}};
        const float w = size.x, h = size.y, d = size.z;
        const float uv[6][4] = {{u+d+w, v+d, u+d+w+d, v+d+h},
                                {float(u), v+d, u+d, v+d+h},
                                {u+d, float(v), u+d+w, v+d},
                                {u+d+w, v+d, u+d+w+w, float(v)},
                                {u+d, v+d, u+d+w, v+d+h},
                                {u+d+w+d, v+d, u+d+w+d+w, v+d+h}};
        for (int i = 0; i < 6; ++i)
            face(points, faces[i], uv[i][0], uv[i][1], uv[i][2], uv[i][3], mirror, projection);
    }
};
struct ClipVertex { Vec4 p; float u,v; };
float clipDistance(const Vec4& p,int plane) {
    switch(plane) {
    case 0:return p.w+p.x; case 1:return p.w-p.x;
    case 2:return p.w+p.y; case 3:return p.w-p.y;
    case 4:return p.w+p.z; default:return p.w-p.z;
    }
}
Vertex windowVertex(const ClipVertex& v,int w,int h) {
    const float q=1/v.p.w;
    return {(v.p.x*q+1)*0.5f*w,(1-v.p.y*q)*0.5f*h,q,v.u*q,v.v*q};
}
void originalFace(Mesh& mesh,const std::array<Vec3,8>& camera,const int* indices,
                  const std::array<FirstPersonUV,4>& uv,const Matrix& projection,
                  int w,int h,int u0,int v0,int u1,int v1,
                  const uint32_t* texture=steve::kSkinPixels,int textureWidth=64,
                  uint32_t tint=0xffffff,bool exactIconColors=false) {
    const Vec3 a=camera[indices[0]],b=camera[indices[1]],c=camera[indices[2]];
    const Vec3 n=cross(subtract(b,a),subtract(c,a));
    if(n.x*a.x+n.y*a.y+n.z*a.z>=0) return;
    std::array<ClipVertex,16> polygon{},output{};
    int count=4;
    for(int i=0;i<4;++i) {
        const Vec3 p=camera[indices[i]];
        polygon[i]={projection.apply({p.x,p.y,p.z,1}),uv[i].u,uv[i].v};
    }
    // Homogeneous clipping before perspective divide, including the near plane.
    for(int plane=0;plane<6 && count;++plane) {
        int out=0;
        ClipVertex previous=polygon[count-1];
        float d0=clipDistance(previous.p,plane);
        for(int i=0;i<count;++i) {
            const ClipVertex next=polygon[i];const float d1=clipDistance(next.p,plane);
            if((d0>=0)!=(d1>=0)) {
                const float t=d0/(d0-d1),s=1-t;
                output[out++]={{previous.p.x*s+next.p.x*t,previous.p.y*s+next.p.y*t,
                                previous.p.z*s+next.p.z*t,previous.p.w*s+next.p.w*t},
                               previous.u*s+next.u*t,previous.v*s+next.v*t};
            }
            if(d1>=0) output[out++]=next;
            previous=next;d0=d1;
        }
        polygon=output;count=out;
    }
    if(count<3) return;
    // Deliberately platform shading; original light-layer/GPU lighting is not ported.
    const float length=std::sqrt(n.x*n.x+n.y*n.y+n.z*n.z);
    const unsigned light=exactIconColors?255:unsigned(192+63*std::max(0.0f,(-.32f*n.x+.56f*n.y+.76f*n.z)/length));
    // A convex quad clipped against six planes has at most ten vertices/eight
    // triangles. All callers reserve that bound; overflow is not a fill policy.
    assert(mesh.count+size_t(count-2)<=mesh.capacity);
    for(int i=1;i+1<count;++i)
        mesh.triangles[mesh.count++]={windowVertex(polygon[0],w,h),windowVertex(polygon[i],w,h),
            windowVertex(polygon[i+1],w,h),light,u0,v0,u1,v1,texture,textureWidth,tint};
}
std::array<int,4> meshBounds(const Mesh& mesh,int w,int h) {
    float x0=float(w),y0=float(h),x1=0,y1=0;
    for(size_t i=0;i<mesh.count;++i)
        for(const Vertex& v:{mesh.triangles[i].a,mesh.triangles[i].b,mesh.triangles[i].c}) {
            x0=std::min(x0,v.x);y0=std::min(y0,v.y);x1=std::max(x1,v.x);y1=std::max(y1,v.y);
        }
    // Clamp BEFORE float-to-int conversion, including extreme skinny viewports.
    return {{int(std::floor(std::clamp(x0,0.0f,float(w)))),
             int(std::floor(std::clamp(y0,0.0f,float(h)))),
             int(std::ceil(std::clamp(x1,0.0f,float(w)))),
             int(std::ceil(std::clamp(y1,0.0f,float(h))))}};
}
// ---- Held TileItems (ItemInHandRenderer::renderItem, ItemInHandRenderer.cpp:241) ----
// A TileItem whose tile render shape passes TileRenderer::canRender
// (TileRenderer.cpp:8512) is drawn by TileRenderer::renderTile(tile, aux)
// (TileRenderer.cpp:7925) as 3D geometry; every other TileItem goes down the
// same extruded-icon branch as a plain Item (ItemInHandRenderer.cpp:253-301).
// Catalog shapes map to source render shapes as follows:
//   SHAPE_BLOCK  (Tile.cpp:632): cube, grass, leaves, slab, snow, button,
//                pressure_plate, carpet; drawn with tile->updateDefaultShape()
//   SHAPE_PISTON_BASE (PistonBaseTile.cpp:112) piston, data forced Facing::UP
//   SHAPE_PORTAL_FRAME (TheEndPortalFrameTile.cpp:46) endframe, 13/16 high
//   SHAPE_ENTITYTILE_ANIMATED (ChestTile.cpp:37) chest
//   SHAPE_CACTUS (CactusTile.cpp:74), SHAPE_STAIRS (StairTile.cpp:47),
//   SHAPE_FENCE (FenceTile.cpp:112), SHAPE_WALL (WallTile.cpp:34)
// Everything else (cross, torch, ladder, rail, lever, vine, lilypad, pane,
// tripwire_hook, ...) is not in canRender and is held as its icon sprite.
struct HeldQuad {
    std::array<Vec3,4> p;              // tile-local [0,1]^3 (fence rails exceed it)
    std::array<FirstPersonUV,4> uv;    // atlas texels
    int u0, v0, u1, v1;                // inclusive atlas texel limits
    uint32_t tint;
};
struct HeldModel {
    HeldBlockPath path = HeldBlockPath::None;
    // The SHAPE_BLOCK/TREE/QUARTZ/PISTON_BASE/PORTAL_FRAME and
    // ENTITYTILE_ANIMATED branches add glRotatef(90,0,1,0) + translate(-.5)
    // (TileRenderer.cpp:7963-7965, 8151-8152); CACTUS/STAIRS/FENCE/WALL only
    // translate (TileRenderer.cpp:8107, 8177, 8277, 8364).
    bool rotated = true;
    std::vector<HeldQuad> quads;
    std::array<uint32_t,16*16> sprite{}; // terrain icon, same layout as itemicons
    uint32_t spriteTint = 0xffffff;
};
const legacy::IconSpec* findAtlasIcon(const wchar_t* name) {
    for(const auto& spec:legacy::kIconSpecs) if(std::wstring(spec.name)==name) return &spec;
    return nullptr;
}
// Item colour the source applies (glColor before renderItem, ItemInHandRenderer.cpp:449;
// renderTile re-applies tile->getColor(data), TileRenderer.cpp:7936). The catalog
// colour is a WORLD biome sample (e.g. vine 5bab46), not the item colour. Values are
// port/assets/colours.xml: Foliage_Default 48b518, Evergreen 619961, Birch 80a755,
// Tile_WaterLily 208030. Item::getColor defaults to white (Item.cpp:848).
uint32_t heldItemColour(const legacy::BlockSpec& block) {
    switch(block.tileId) {
    case 2: return block.color; // GrassTile top only; handled by the grass flag
    case 18: // LeafTileItem::getColor / LeafTile::getColor(data) (LeafTile.cpp:41)
        if((block.data&3)==1) return 0x619961;
        if((block.data&3)==2) return 0x80a755;
        return 0x48b518;
    case 31: // ColoredTileItem -> TallGrass::getColor(aux) (TallGrass.cpp:36)
        return (block.data==0)?0xffffff:0x48b518;
    case 106: return 0x48b518; // ColoredTileItem -> VineTile::getColor(aux) (VineTile.cpp:181)
    case 111: return 0x208030; // WaterLilyTileItem -> WaterlilyTile::getColor (WaterLilyTile.cpp:47)
    default: return block.color; // white for every other held catalog entry
    }
}
struct HeldBox { float x0,y0,z0,x1,y1,z1; };
// Emit the six faces of a box with the vertex order of the existing unit cube
// (kCubeFaces/kCubeUV) and the sub-rectangle UVs of renderFaceDown/Up/North/
// South/West/East (TileRenderer.cpp:7200, 7310, 7423, 7542, 7661, 7779):
// down/up u=X0..X1 v=Z0..Z1, north/south u=X0..X1 v=16-Y1..16-Y0,
// west/east u=Z0..Z1 v=16-Y1..16-Y0; an axis outside [0,1] uses the full icon.
// inset moves the four side faces inwards (cactus t->addOffset, 8121-8143).
void heldBoxQuads(HeldModel& model,const HeldBox& b,const wchar_t* const faces[6],
                  uint32_t tint,bool grass,float inset=0,bool fullUV=false) {
    const float lo[3]={b.x0,b.y0,b.z0},hi[3]={b.x1,b.y1,b.z1};
    for(int face=0;face<6;++face) {
        const auto* icon=findAtlasIcon(faces[face]);
        if(!icon) continue;
        const float iu0=icon->u0*legacy::kAtlasWidth,iv0=icon->v0*legacy::kAtlasHeight;
        const float iu1=icon->u1*legacy::kAtlasWidth,iv1=icon->v1*legacy::kAtlasHeight;
        const int uAxis=face<4?0:2;
        const bool vIsZ=face<2;
        const auto range=[&](int axis,bool invert,float& a,float& c) {
            if(fullUV || lo[axis]<0 || hi[axis]>1) {a=0;c=1;return;}
            a=invert?1-hi[axis]:lo[axis]; c=invert?1-lo[axis]:hi[axis];
        };
        float ua,ub,va,vb;
        range(uAxis,false,ua,ub);
        range(vIsZ?2:1,!vIsZ,va,vb);
        HeldQuad q;
        for(int i=0;i<4;++i) {
            const int k=originalfp::kCubeFaces[face][i];
            // Point layout {x0,y0,z0},{x1,y0,z0},{x1,y1,z0},{x0,y1,z0},{x0,y0,z1}...
            const bool fx=k==1||k==2||k==5||k==6, fy=k==2||k==3||k==6||k==7, fz=k>=4;
            Vec3 p{fx?b.x1:b.x0,fy?b.y1:b.y0,fz?b.z1:b.z0};
            if(inset!=0) {
                if(face==2) p.z+=inset; else if(face==3) p.z-=inset;
                else if(face==4) p.x+=inset; else if(face==5) p.x-=inset;
            }
            q.p[i]=p;
            const float fu=originalfp::kCubeUV[face][i][0]?ub:ua;
            const float fv=originalfp::kCubeUV[face][i][1]?vb:va;
            q.uv[i]={iu0+(iu1-iu0)*fu,iv0+(iv1-iv0)*fv};
        }
        const float minU=iu0+(iu1-iu0)*ua,maxU=iu0+(iu1-iu0)*ub;
        const float minV=iv0+(iv1-iv0)*va,maxV=iv0+(iv1-iv0)*vb;
        q.u0=int(std::floor(minU+1e-4f));q.v0=int(std::floor(minV+1e-4f));
        q.u1=std::max(q.u0,int(std::ceil(maxU-1e-4f))-1);
        q.v1=std::max(q.v0,int(std::ceil(maxV-1e-4f))-1);
        q.tint=grass?(face==1?tint:0xffffff):tint;
        model.quads.push_back(q);
    }
}
HeldModel buildHeldModel(int index) {
    HeldModel model;
    const auto& block=legacy::kBlockSpecs[index];
    const std::string_view shape=block.shape;
    const uint32_t colour=heldItemColour(block);
    const bool grass=shape=="grass";
    const auto box=[&](HeldBox b,float inset=0,bool fullUV=false) {
        heldBoxQuads(model,b,block.faces,grass?block.color:colour,grass,inset,fullUV);
    };
    // Reuse the world geometry where the item shape IS the default world shape.
    const auto world=[&](int data) {
        legacy::BlockSpec copy=block; copy.data=data;
        for(const auto& b:worldrender::buildGeometry(copy).boxes)
            box({b.low.x,b.low.y,b.low.z,b.high.x,b.high.y,b.high.z});
    };
    constexpr float P=1/16.0f;
    model.path=HeldBlockPath::Block3D;
    if((shape=="cube" && block.height==1) || grass || shape=="leaves" || shape=="piston") {
        // renderTile SHAPE_BLOCK: updateDefaultShape() = full cell. Leaves keep
        // cutout alpha (alpha-0 texels are skipped). Piston: data=Facing::UP
        // (TileRenderer.cpp:7959) = the catalog's top/bottom/side faces.
        box({0,0,0,1,1,1});
    } else if(shape=="slab") {
        world(block.data&7); // HalfSlabTile::updateDefaultShape (0,0,0,1,.5,1) (HalfSlabTile.cpp:45)
    } else if(shape=="snow") {
        world(0);            // TopSnowTile::updateShape(0) = 2/16 (TopSnowTile.cpp:66)
    } else if(shape=="carpet") {
        world(0);            // WoolCarpetTile::updateShape(0) = 1/16 (WoolCarpetTile.cpp:54)
    } else if(shape=="pressure_plate") {
        // BasePressurePlateTile::updateDefaultShape: centred 16x4x16 (BasePressurePlateTile.cpp:173).
        box({0,.5f-2*P,0,1,.5f+2*P,1});
    } else if(shape=="button") {
        // ButtonTile::updateDefaultShape: centred 6x4x4 (ButtonTile.cpp:276).
        box({.5f-3*P,.5f-2*P,.5f-2*P,.5f+3*P,.5f+2*P,.5f+2*P});
    } else if(shape=="endframe") {
        // SHAPE_PORTAL_FRAME takes the SHAPE_BLOCK branch first (TileRenderer.cpp:7955) with
        // TheEndPortalFrameTile::updateDefaultShape 13/16 (TheEndPortalFrameTile.cpp:51).
        box({0,0,0,1,13*P,1});
    } else if(shape=="chest") {
        // SHAPE_ENTITYTILE_ANIMATED -> EntityTileRenderer -> ChestRenderer with data 0
        // (ChestRenderer.cpp:47, rot 0): the ChestModel (ChestModel.cpp:7-20) is a
        // 14x14x14 box, latch on +Z. The chest_* atlas icons are composited from
        // that model's chest.png faces (tools/generate-terrain.py:289), so draw
        // the world chest box with those icons, front moved to the latch side.
        // Platform choice: the 2x4x1 latch is part of chest_front, not a cuboid.
        const wchar_t* faces[6]={block.faces[0],block.faces[1],block.faces[3],
                                 block.faces[2],block.faces[4],block.faces[5]};
        heldBoxQuads(model,{P,0,P,15*P,14*P,15*P},faces,colour,false,0,true);
    } else if(shape=="cactus") {
        // SHAPE_CACTUS: full cell, sides offset 1/16 inwards (TileRenderer.cpp:8104).
        model.rotated=false;
        box({0,0,0,1,1,1},P);
    } else if(shape=="stairs") {
        // SHAPE_STAIRS item boxes (TileRenderer.cpp:8172-8173).
        model.rotated=false;
        box({0,0,0,1,1,.5f});
        box({0,0,.5f,1,.5f,1});
    } else if(shape=="fence") {
        // SHAPE_FENCE item boxes (TileRenderer.cpp:8271-8276): two posts, two rails.
        model.rotated=false;
        float w=2*P;
        box({.5f-w,0,0,.5f+w,1,w*2});
        box({.5f-w,0,1-w*2,.5f+w,1,1});
        w=P;
        box({.5f-w,1-w*3,-w*2,.5f+w,1-w,1+w*2});
        box({.5f-w,.5f-w*3,-w*2,.5f+w,.5f-w,1+w*2});
    } else if(shape=="wall") {
        // SHAPE_WALL item boxes (TileRenderer.cpp:8361-8362), WallTile.cpp:9-12.
        model.rotated=false;
        box({0,0,.5f-3*P,1,13*P,.5f+3*P});
        box({.5f-4*P,0,.5f-4*P,.5f+4*P,1,.5f+4*P});
    } else if(shape=="air" || shape=="liquid" || shape=="fire" || shape=="door" ||
              shape=="repeater" || shape=="wire" || shape=="crops" || shape=="cocoa" ||
              shape=="flower_pot" || shape=="cauldron" || shape=="tripwire" ||
              shape=="cube") {
        // No TileItem holds these states: liquids/fire have no creative item, and
        // doors/repeater/dust/crops/cocoa/pot/cauldron/string are independent
        // Items with items.png sprites (hasHeldItemSprite). Non-unit "cube"
        // catalog entries stay unsupported rather than guessing their bounds.
        model.path=HeldBlockPath::None;
    } else {
        // Not in canRender: TileItem::getIcon(aux) = tile->getTexture(Facing::UP, aux)
        // (TileItem.cpp:40), drawn like any item from the terrain atlas.
        model.path=HeldBlockPath::Sprite;
        model.spriteTint=colour;
        const auto* icon=findAtlasIcon(block.faces[1]);
        if(!icon) {model.path=HeldBlockPath::None;return model;}
        const int x0=int(std::lround(icon->u0*legacy::kAtlasWidth));
        const int y0=int(std::lround(icon->v0*legacy::kAtlasHeight));
        for(int y=0;y<16;++y) for(int x=0;x<16;++x)
            model.sprite[size_t(y)*16+x]=legacy::kAtlasPixels[size_t(y0+y)*legacy::kAtlasWidth+x0+x];
    }
    if(model.path==HeldBlockPath::Block3D && model.quads.empty()) model.path=HeldBlockPath::None;
    return model;
}
// Built lazily once per catalog index (the first frame an item is held), never
// rebuilt: no per-frame icon-name search or mesh construction. Building all 544
// eagerly costs ~27 ms on the host (icon-name search), too long for one frame.
const HeldModel& cachedHeldModel(int index) {
    thread_local std::vector<std::unique_ptr<HeldModel>> models(legacy::kBlockCount);
    auto& slot=models[size_t(index)];
    if(!slot) slot=std::make_unique<HeldModel>(buildHeldModel(index));
    return *slot;
}
void buildOriginalHeldBlock(Mesh& mesh,const Matrix& model,const Matrix& projection,
                            int w,int h,const HeldModel& held,
                            FirstPersonDiagnostics& diagnostics) {
    // renderTile is called with its default fAlpha=1; catalog world-overlay
    // opacity (e.g. glass .65) is NOT a held-item transform/material parameter.
    diagnostics.heldBlockSupported=true;
    static constexpr int order[4]={0,1,2,3};
    for(size_t i=0;i<held.quads.size();++i) {
        const HeldQuad& q=held.quads[i];
        std::array<Vec3,8> camera{};
        for(int k=0;k<4;++k) {
            const Vec4 c=model.apply({q.p[k].x,q.p[k].y,q.p[k].z,1});
            camera[k]={c.x,c.y,c.z};
        }
        if(i<6) diagnostics.faceUV[i]=q.uv;
        originalFace(mesh,camera,order,q.uv,projection,w,h,q.u0,q.v0,q.u1,q.v1,
                     legacy::kAtlasPixels,legacy::kAtlasWidth,q.tint);
    }
}
uint32_t shade(uint32_t pixel, unsigned light)
{
    const unsigned r = ((pixel >> 16) & 255) * light / 255;
    const unsigned g = ((pixel >> 8) & 255) * light / 255;
    const unsigned b = (pixel & 255) * light / 255;
    return (pixel & 0xff000000u) | (r << 16) | (g << 8) | b;
}
uint32_t over(uint32_t source, uint32_t destination)
{
    const unsigned sa = source >> 24;
    if (sa == 255) return source;
    const unsigned da = destination >> 24;
    const unsigned a255 = sa * 255 + da * (255 - sa);
    if (!a255) return destination;
    uint32_t result = ((a255 + 127) / 255) << 24;
    for (int shift : {0, 8, 16}) {
        const unsigned channel = (((source >> shift) & 255) * sa * 255 +
                                  ((destination >> shift) & 255) * da * (255 - sa) +
                                  a255 / 2) / a255;
        result |= channel << shift;
    }
    return result;
}
void draw(uint32_t* pixels, int w, int h, const Mesh& mesh)
{
    if (!mesh.count) return;
    const auto bounds=meshBounds(mesh,w,h);
    const int left=bounds[0],top=bounds[1],right=bounds[2],bottom=bounds[3];
    if (right <= left || bottom <= top) return;
    const int stride = right - left;
    const size_t depthSize = size_t(stride) * (bottom - top);
    thread_local std::vector<float> depth;
    depth.resize(depthSize);
    std::fill(depth.begin(), depth.end(), 0.0f);
    for (size_t i = 0; i < mesh.count; ++i) {
        Triangle tri = mesh.triangles[i];
        float area = edge(tri.a, tri.b, tri.c.x, tri.c.y);
        if (std::abs(area) < 0.00001f) continue;
        if (area < 0) { std::swap(tri.b, tri.c); area = -area; }
        const int x0 = std::max(left, int(std::floor(std::min({tri.a.x, tri.b.x, tri.c.x}))));
        const int x1 = std::min(right, int(std::ceil(std::max({tri.a.x, tri.b.x, tri.c.x}))));
        const int y0 = std::max(top, int(std::floor(std::min({tri.a.y, tri.b.y, tri.c.y}))));
        const int y1 = std::min(bottom, int(std::ceil(std::max({tri.a.y, tri.b.y, tri.c.y}))));
        // Shade at most 8x12 source texels once per face, not per output pixel.
        // Every triangle still samples perspective-correct UVs at full output size.
        std::array<uint32_t, 256> texels{};
        const int textureStride = tri.u1 - tri.u0 + 1;
        if (textureStride<=0 || tri.v1<tri.v0 ||
            size_t(textureStride)*size_t(tri.v1-tri.v0+1)>texels.size()) continue;
        for (int v = tri.v0; v <= tri.v1; ++v)
            for (int u = tri.u0; u <= tri.u1; ++u) {
                uint32_t color=tri.texture[v * tri.textureWidth + u];
                if(tri.tint!=0xffffff) {
                    uint32_t tinted=color&0xff000000u;
                    for(int shift:{0,8,16}) tinted|=((((color>>shift)&255)*((tri.tint>>shift)&255)/255)<<shift);
                    color=tinted;
                }
                texels[size_t(v - tri.v0) * textureStride + u - tri.u0] =shade(color,tri.light);
            }
        const float invArea = 1.0f / area;
        const float ax = -(tri.c.y - tri.b.y), ay = tri.c.x - tri.b.x;
        const float bx = -(tri.a.y - tri.c.y), by = tri.a.x - tri.c.x;
        const float cx = -(tri.b.y - tri.a.y), cy = tri.b.x - tri.a.x;
        float waRow = edge(tri.b, tri.c, x0 + 0.5f, y0 + 0.5f);
        float wbRow = edge(tri.c, tri.a, x0 + 0.5f, y0 + 0.5f);
        float wcRow = edge(tri.a, tri.b, x0 + 0.5f, y0 + 0.5f);
        const float dq = (ax * tri.a.q + bx * tri.b.q + cx * tri.c.q) * invArea;
        const float duq = (ax * tri.a.uq + bx * tri.b.uq + cx * tri.c.uq) * invArea;
        const float dvq = (ax * tri.a.vq + bx * tri.b.vq + cx * tri.c.vq) * invArea;
        for (int y = y0; y < y1; ++y) {
            float wa = waRow, wb = wbRow, wc = wcRow;
            float q = (wa * tri.a.q + wb * tri.b.q + wc * tri.c.q) * invArea;
            float uq = (wa * tri.a.uq + wb * tri.b.uq + wc * tri.c.uq) * invArea;
            float vq = (wa * tri.a.vq + wb * tri.b.vq + wc * tri.c.vq) * invArea;
            size_t zi = size_t(y - top) * stride + size_t(x0 - left);
            size_t pi = size_t(y) * w + size_t(x0);
            for (int x = x0; x < x1; ++x, ++zi, ++pi) {
                if (wa >= 0 && wb >= 0 && wc >= 0 && q > depth[zi]) {
                    const float z = 1.0f / q;
                    const int u = std::max(tri.u0, std::min(tri.u1, int(uq * z)));
                    const int v = std::max(tri.v0, std::min(tri.v1, int(vq * z)));
                    const uint32_t texel = texels[size_t(v - tri.v0) * textureStride + u - tri.u0];
                    if ((texel >> 24) == 255) {
                        pixels[pi] = texel;
                        depth[zi] = q;
                    } else if (texel >> 24) {
                        pixels[pi] = over(texel, pixels[pi]);
                        depth[zi] = q; // transparent texels never hide another face
                    }
                }
                wa += ax; wb += bx; wc += cx;
                q += dq; uq += duq; vq += dvq;
            }
            waRow += ay; wbRow += by; wcRow += cy;
        }
    }
}
} // namespace

void renderOriginalHand(uint32_t* pixels,int w,int h,const FirstPersonState& input,
                        FirstPersonDiagnostics* diagnostics)
{
    if(diagnostics) *diagnostics={};
    if(w<=0 || h<=0 || w>16384 || h>16384) return;
    // Not named `finite`: uClibc <math.h> defines a one-argument finite() macro.
    auto finiteOr=[](float value,float fallback) { return std::isfinite(value)?value:fallback; };
    FirstPersonState state=input;
    state.swing=std::clamp(finiteOr(state.swing,0),0.0f,1.0f);
    state.equip=std::clamp(finiteOr(state.equip,1),0.0f,1.0f);
    float aspectScale=1,fovScale=1,fudgeX=0,fudgeY=0;
    if(state.viewport==FirstPersonViewport::SplitHorizontal) {
        aspectScale=originalfp::kHorizontalAspect;fovScale=originalfp::kHorizontalFov;
        fudgeY=originalfp::kHorizontalFudgeY;
    } else if(state.viewport==FirstPersonViewport::SplitVertical) {
        aspectScale=originalfp::kVerticalAspect;fudgeX=originalfp::kVerticalFudgeX;
    }
    float aspect=finiteOr(state.aspect,0);
    if(aspect<=0) aspect=float(w)/h/aspectScale;
    // Finite extreme inputs must not overflow reciprocal perspective or raster ints.
    aspect=std::clamp(aspect*aspectScale,1.0f/65536,65536.0f);
    const float fov=std::clamp(finiteOr(state.fov,originalfp::kDefaultFov),1.0f,179.0f)*fovScale;
    const float near=originalfp::kNearPlane;
    const float far=std::clamp(finiteOr(state.farPlane,512),near+0.01f,1.0e6f);
    const float cot=1/std::tan(fov*kPi/360);
    Matrix projection;
    projection.m={{cot/aspect,0,0,0, 0,cot,0,0,
                   0,0,-(far+near)/(far-near),-2*far*near/(far-near), 0,0,-1,0}};
    Matrix model;
    if(state.bob) applyOriginalBob(model,std::remainder(finiteOr(state.bobPhase,0),2.0f),
        std::clamp(finiteOr(state.bobAmount,0),0.0f,1.0f),
        std::remainder(finiteOr(state.bobTilt,0),360.0f));
    applyOriginalViewLag(model,std::remainder(finiteOr(state.pitchLag,0),360.0f),
                              std::remainder(finiteOr(state.yawLag,0),360.0f));
    // Inventory selection is explicit. Unsupported shapes never masquerade as
    // empty hand; the block subset is reported by diagnostics below.
    const bool held=state.heldBlockIndex>=0;
    const HeldModel* heldModel=held && state.heldBlockIndex<legacy::kBlockCount?
        &cachedHeldModel(state.heldBlockIndex):nullptr;
    if(held) {
        applyOriginalHeldBlock(model,state.swing,state.equip,fudgeX,fudgeY,0);
        // Shapes without renderTile's glRotatef(90,0,1,0): undo it around the
        // cell centre, leaving exactly the translate(-.5) those branches apply.
        if(heldModel && heldModel->path==HeldBlockPath::Block3D && !heldModel->rotated) {
            model.translate(.5f,.5f,.5f);model.rotate(-90,0,1,0);model.translate(-.5f,-.5f,-.5f);
        }
    } else {
        applyOriginalEmptyHand(model,state.swing,state.equip,fudgeX,fudgeY,0);
        model.translate(originalfp::kArmPivot[0]*originalfp::kModelScale,
                        originalfp::kArmPivot[1]*originalfp::kModelScale,
                        originalfp::kArmPivot[2]*originalfp::kModelScale);
        model.rotate(originalfp::kRestZRadians*180/kPi,0,0,1);
        model.scale(originalfp::kModelScale,originalfp::kModelScale,originalfp::kModelScale);
    }
    // Fence = 24 quads, each at most eight triangles after six-plane clipping.
    thread_local std::array<Triangle,64*8> storage;
    Mesh mesh(storage.data(),storage.size());
    std::array<Vec3,8> camera{};
    FirstPersonDiagnostics local;
    auto& d=diagnostics?*diagnostics:local;
    d.valid=true;d.aspect=aspect;d.fov=fov;d.modelView=model.m;d.projection=projection.m;
    d.heldPath=heldModel?heldModel->path:HeldBlockPath::None;
    static constexpr int faces[6][4]={{5,1,2,6},{0,4,7,3},{5,4,0,1},
                                     {2,3,7,6},{1,0,3,2},{4,5,6,7}};
    const float x0=held?0:originalfp::kArmOrigin[0],y0=held?0:originalfp::kArmOrigin[1],
                z0=held?0:originalfp::kArmOrigin[2];
    const float x1=x0+(held?1:originalfp::kArmSize[0]),y1=y0+(held?1:originalfp::kArmSize[1]),
                z1=z0+(held?1:originalfp::kArmSize[2]);
    const std::array<Vec3,8> points{{{x0,y0,z0},{x1,y0,z0},{x1,y1,z0},{x0,y1,z0},
                                    {x0,y0,z1},{x1,y0,z1},{x1,y1,z1},{x0,y1,z1}}};
    for(int i=0;i<8;++i) {
        const Vec3 p=points[i];d.modelVertices[i]={p.x,p.y,p.z};
        const Vec4 c=model.apply({p.x,p.y,p.z,1});camera[i]={c.x,c.y,c.z};
        d.cameraVertices[i]={c.x,c.y,c.z};
        const Vec4 clip=projection.apply(c);
        if(clip.w>0) {
            const Vertex v=windowVertex({clip,0,0},w,h);
            bool inside=true;for(int plane=0;plane<6;++plane) inside &= clipDistance(clip,plane)>=0;
            d.projectedVertices[i]={v.x,v.y,clip.w,inside};
        }
    }
    if(!held) {
        const float u=originalfp::kArmUv[0],v=originalfp::kArmUv[1],
                    a=originalfp::kArmSize[0],b=originalfp::kArmSize[1],c=originalfp::kArmSize[2];
        const float uv[6][4]={{u+c+a,v+c,u+c+a+c,v+c+b},{u,v+c,u+c,v+c+b},
            {u+c,v,u+c+a,v+c},{u+c+a,v+c,u+c+a+a,v},
            {u+c,v+c,u+c+a,v+c+b},{u+c+a+c,v+c,u+c+a+c+a,v+c+b}};
        for(int face=0;face<6;++face) {
            const float* t=uv[face];const float inset=originalfp::kUvInsetTexels;
            const float us=t[2]>t[0]?inset:-inset,vs=t[3]>t[1]?inset:-inset;
            d.faceUV[face]={{{t[2]-us,t[1]+vs},{t[0]+us,t[1]+vs},
                            {t[0]+us,t[3]-vs},{t[2]-us,t[3]-vs}}};
            originalFace(mesh,camera,faces[face],d.faceUV[face],projection,w,h,
                int(std::min(t[0],t[2])),int(std::min(t[1],t[3])),
                int(std::max(t[0],t[2]))-1,int(std::max(t[1],t[3]))-1);
        }
    } else if(heldModel && heldModel->path==HeldBlockPath::Block3D) {
        buildOriginalHeldBlock(mesh,model,projection,w,h,*heldModel,d);
    }
    // Sprite-path tiles draw nothing here: renderHeldItemOverlay owns them.
    d.triangleCount=mesh.count;d.rasterBounds=meshBounds(mesh,w,h);
    if(pixels) draw(pixels,w,h,mesh);
}

bool hasHeldItemSprite(int itemId,int aux)
{
    const std::string key="item:"+std::to_string(itemId)+":"+std::to_string(aux);
    if(!itemicons::findIcon(key.c_str())) return false;
    for(const auto& candidate:originalfp::kHeldSprites)
        if(candidate.itemId==itemId && candidate.aux==aux) return true;
    return false;
}

HeldBlockPath heldBlockPath(int blockIndex)
{
    if(blockIndex<0 || blockIndex>=legacy::kBlockCount) return HeldBlockPath::None;
    return cachedHeldModel(blockIndex).path;
}

namespace {
// ItemInHandRenderer::renderItem's extruded icon (listItem/listTerrain share the
// same 16x16 texel-cube layout, ItemInHandRenderer.cpp:40-115). pixels is the
// 16x16 icon in itemicons layout; tint is Item::getColor (glColor, line 449).
void renderHeldSprite(uint32_t* target,int w,int h,const uint32_t* icon,uint32_t tint,
                      bool mirrored,float swing,float equip)
{
    swing=std::isfinite(swing)?std::clamp(swing,0.0f,1.0f):0;
    equip=std::isfinite(equip)?std::clamp(equip,0.0f,1.0f):1;
    Matrix model;
    // isHandEquipped is audited class metadata, NOT a first-person pose switch:
    // the source only uses that switch in third-person PlayerRenderer. Tools
    // and materials share this branch; their actual icon geometry differs.
    applyOriginalHeldSprite(model,swing,equip,0,0,0,mirrored);
    const float near=originalfp::kNearPlane,far=512;
    const float cot=1/std::tan(originalfp::kDefaultFov*kPi/360);
    Matrix projection;
    projection.m={{cot/(float(w)/h),0,0,0, 0,cot,0,0,
                   0,0,-(far+near)/(far-near),-2*far*near/(far-near), 0,0,-1,0}};
    // 256 texel cubes * six quads * eight triangles after six-plane clipping.
    // One draw call means ONE depth rectangle across all texels and side faces.
    thread_local std::array<Triangle,256*6*8> storage;
    Mesh mesh(storage.data(),storage.size());
    constexpr int size=itemicons::kIconSize;
    static_assert(size==16,"Original sprite extrusion uses 16x16 texels");
    auto opaque=[&](int x,int y) {
        return x>=0 && y>=0 && x<size && y<size &&
               (icon[(size-1-y)*size+size-1-x]>>24)==255;
    };
    for(int y=0;y<size;++y) for(int x=0;x<size;++x) {
        const int u=size-1-x,v=size-1-y;
        if(!(icon[v*size+u]>>24)) continue;
        const float x0=float(x)/size,x1=float(x+1)/size;
        const float y0=float(y)/size,y1=float(y+1)/size;
        const float z=-originalfp::kSpriteDepth;
        std::array<Vec3,8> camera{{{x0,y0,0},{x1,y0,0},{x1,y1,0},{x0,y1,0},
                                  {x0,y0,z},{x1,y0,z},{x1,y1,z},{x0,y1,z}}};
        for(auto& p:camera) {
            const auto c=model.apply({p.x,p.y,p.z,1});p={c.x,c.y,c.z};
        }
        // Original listItem samples each texel's center on all six cube faces.
        // Alpha-zero cubes are omitted, preserving holes in color AND depth.
        const std::array<FirstPersonUV,4> uv{{{u+.5f,v+.5f},{u+.5f,v+.5f},
                                              {u+.5f,v+.5f},{u+.5f,v+.5f}}};
        const bool hidden[6]={false,false,opaque(x-1,y),opaque(x+1,y),
                             opaque(x,y-1),opaque(x,y+1)};
        for(int face=0;face<6;++face) if(!hidden[face])
            originalFace(mesh,camera,originalfp::kSpriteFaces[face],uv,projection,w,h,
                         u,v,u,v,icon,size,tint,true);
    }
    draw(target,w,h,mesh);
}
// Canonical item id/aux -> catalog render index, cached for the per-frame caller.
int heldTileIndex(int itemId,int aux)
{
    thread_local int lastId=-2,lastAux=-2,lastIndex=-1;
    if(itemId!=lastId || aux!=lastAux) {
        const int item=sourceitems::itemIndexForIdAux(itemId,aux);
        lastId=itemId;lastAux=aux;
        lastIndex=item>=0?sourceitems::kItems[item].renderIndex:-1;
    }
    return lastIndex;
}
} // namespace

bool hasHeldTileSprite(int itemId,int aux)
{
    return !hasHeldItemSprite(itemId,aux) &&
           heldBlockPath(heldTileIndex(itemId,aux))==HeldBlockPath::Sprite;
}

void renderHeldItemOverlay(uint32_t* pixels,int w,int h,int itemId,int aux,float swing,float equip)
{
    if(!pixels || w<=0 || h<=0 || w>16384 || h>16384) return;
    // Do not mask aux, consult world state, infer block indices, or fall back to
    // an arbitrary damage-zero icon. The caller supplies a canonical identity.
    const std::string key="item:"+std::to_string(itemId)+":"+std::to_string(aux);
    const auto* icon=itemicons::findIcon(key.c_str());
    const originalfp::HeldSpriteSpec* spec=nullptr;
    if(icon) for(const auto& candidate:originalfp::kHeldSprites)
        if(candidate.itemId==itemId && candidate.aux==aux) {spec=&candidate;break;}
    if(icon && spec) {
        renderHeldSprite(pixels,w,h,icon->pixels,0xffffff,spec->mirrored,swing,equip);
        return;
    }
    // TileItem outside canRender: its terrain icon, tinted by the item colour.
    // TileItem/ColoredTileItem never set isMirroredArt (Item.cpp default false).
    const int index=heldTileIndex(itemId,aux);
    if(heldBlockPath(index)!=HeldBlockPath::Sprite) return;
    const HeldModel& model=cachedHeldModel(index);
    renderHeldSprite(pixels,w,h,model.sprite.data(),model.spriteTint,false,swing,equip);
}

void renderActorOverlay(uint32_t* pixels, int w, int h, int viewMode,
                        float walkPhase, float swing, int heldBlockIndex)
{
    if (!pixels || w <= 0 || h <= 0 || w > 16384 || h > 16384 ||
        (viewMode != 0 && viewMode != 1)) return;
    if(viewMode==0) {
        FirstPersonState state;state.swing=swing;state.heldBlockIndex=heldBlockIndex;
        renderOriginalHand(pixels,w,h,state);
        return;
    }
    if (!std::isfinite(walkPhase)) walkPhase = 0;
    walkPhase = std::remainder(walkPhase, 2 * kPi);
    if (!std::isfinite(swing)) swing = 0;
    swing = std::max(0.0f, std::min(1.0f, swing));
    const float stride = std::sin(walkPhase);
    const float action = (swing > 0 && swing < 1) ? std::sin(kPi * swing) : 0;
    const float unit = std::min(float(h), float(w) * 0.8f);
    Mesh mesh;
    {
        // Rear three-quarter preview. Feet remain above the lowest ~12% of
        // the landscape frame; width scales down further on portrait screens.
        const Projection projection{Rotation(0.10f, kPi + 0.30f, 0), 8.0f, 85.0f,
                                    unit * 1.52f, w * 0.50f,
                                    h - unit * (0.42f + 0.004f * std::sin(2 * walkPhase))};
        const Rotation still(0, 0, 0);
        mesh.cube({-4, -8, -4}, {8, 8, 8}, {0, 0, 0},
                  Rotation(0, 0.035f * stride, 0), 0, 0, false, projection);
        mesh.cube({-4, 0, -2}, {8, 12, 4}, {0, 0, 0}, still, 16, 16, false, projection);
        mesh.cube({-3, -2, -2}, {4, 12, 4}, {-5, 2, 0},
                  Rotation(0.55f * stride - 1.10f * action, 0, -0.035f - 0.12f * action),
                  40, 16, false, projection);
        mesh.cube({-1, -2, -2}, {4, 12, 4}, {5, 2, 0},
                  Rotation(-0.55f * stride, 0, 0.035f), 40, 16, true, projection);
        mesh.cube({-2, 0, -2}, {4, 12, 4}, {-2, 12, 0},
                  Rotation(-0.65f * stride, 0, 0), 0, 16, false, projection);
        mesh.cube({-2, 0, -2}, {4, 12, 4}, {2, 12, 0},
                  Rotation(0.65f * stride, 0, 0), 0, 16, true, projection);
    }
    draw(pixels, w, h, mesh);
}

HeldModelView heldModelView(int blockIndex)
{
    HeldModelView view;
    if(blockIndex<0 || blockIndex>=legacy::kBlockCount) return view;
    const HeldModel& model=cachedHeldModel(blockIndex);
    thread_local std::vector<std::unique_ptr<std::vector<HeldModelQuad>>> converted(legacy::kBlockCount);
    auto& quads=converted[size_t(blockIndex)];
    if(!quads) {
        quads=std::make_unique<std::vector<HeldModelQuad>>();
        for(const auto& q:model.quads) {
            HeldModelQuad out{};
            for(int i=0;i<4;++i) {
                out.p[i][0]=q.p[i].x;out.p[i][1]=q.p[i].y;out.p[i][2]=q.p[i].z;
                out.uv[i][0]=q.uv[i].u;out.uv[i][1]=q.uv[i].v;
            }
            out.u0=q.u0;out.v0=q.v0;out.u1=q.u1;out.v1=q.v1;out.tint=q.tint;
            quads->push_back(out);
        }
    }
    view.path=model.path;view.rotated=model.rotated;
    view.quads=quads->data();view.quadCount=quads->size();
    if(model.path==HeldBlockPath::Sprite) {view.sprite=model.sprite.data();view.spriteTint=model.spriteTint;}
    return view;
}
