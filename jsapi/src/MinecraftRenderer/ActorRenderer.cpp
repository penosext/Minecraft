#include "ActorRenderer.hpp"
#include "SteveSkin.hpp"
#include "OriginalFirstPerson.hpp"
#include "LegacyCatalog.hpp"
#include "LegacyTextures.hpp"
#include "ItemIcons.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
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
void buildOriginalHeldCube(Mesh& mesh,const std::array<Vec3,8>& camera,
                           const Matrix& projection,int w,int h,int index,
                           FirstPersonDiagnostics& diagnostics) {
    const auto& block=legacy::kBlockSpecs[index];
    const bool grass=std::string(block.shape)=="grass";
    if((!grass && std::string(block.shape)!="cube") || block.height!=1) return;
    // renderTile is called with its default fAlpha=1; catalog world-overlay
    // opacity (e.g. glass .65) is NOT a held-item transform/material parameter.
    diagnostics.heldBlockSupported=true;
    for(int face=0;face<6;++face) {
        const legacy::IconSpec* icon=nullptr;
        for(const auto& spec:legacy::kIconSpecs)
            if(std::wstring(spec.name)==block.faces[face]) {icon=&spec;break;}
        if(!icon) continue;
        const int u0=int(std::lround(icon->u0*legacy::kAtlasWidth));
        const int v0=int(std::lround(icon->v0*legacy::kAtlasHeight));
        const int u1=int(std::lround(icon->u1*legacy::kAtlasWidth));
        const int v1=int(std::lround(icon->v1*legacy::kAtlasHeight));
        auto& uv=diagnostics.faceUV[face];
        for(int i=0;i<4;++i) uv[i]={float(originalfp::kCubeUV[face][i][0]?u1:u0),
                                                  float(originalfp::kCubeUV[face][i][1]?v1:v0)};
        const uint32_t tint=grass?(face==1?block.color:0xffffff):block.color;
        originalFace(mesh,camera,originalfp::kCubeFaces[face],uv,projection,w,h,u0,v0,u1-1,v1-1,
                     legacy::kAtlasPixels,legacy::kAtlasWidth,tint);
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
    auto finite=[](float value,float fallback) { return std::isfinite(value)?value:fallback; };
    FirstPersonState state=input;
    state.swing=std::clamp(finite(state.swing,0),0.0f,1.0f);
    state.equip=std::clamp(finite(state.equip,1),0.0f,1.0f);
    float aspectScale=1,fovScale=1,fudgeX=0,fudgeY=0;
    if(state.viewport==FirstPersonViewport::SplitHorizontal) {
        aspectScale=originalfp::kHorizontalAspect;fovScale=originalfp::kHorizontalFov;
        fudgeY=originalfp::kHorizontalFudgeY;
    } else if(state.viewport==FirstPersonViewport::SplitVertical) {
        aspectScale=originalfp::kVerticalAspect;fudgeX=originalfp::kVerticalFudgeX;
    }
    float aspect=finite(state.aspect,0);
    if(aspect<=0) aspect=float(w)/h/aspectScale;
    // Finite extreme inputs must not overflow reciprocal perspective or raster ints.
    aspect=std::clamp(aspect*aspectScale,1.0f/65536,65536.0f);
    const float fov=std::clamp(finite(state.fov,originalfp::kDefaultFov),1.0f,179.0f)*fovScale;
    const float near=originalfp::kNearPlane;
    const float far=std::clamp(finite(state.farPlane,512),near+0.01f,1.0e6f);
    const float cot=1/std::tan(fov*kPi/360);
    Matrix projection;
    projection.m={{cot/aspect,0,0,0, 0,cot,0,0,
                   0,0,-(far+near)/(far-near),-2*far*near/(far-near), 0,0,-1,0}};
    Matrix model;
    if(state.bob) applyOriginalBob(model,std::remainder(finite(state.bobPhase,0),2.0f),
        std::clamp(finite(state.bobAmount,0),0.0f,1.0f),
        std::remainder(finite(state.bobTilt,0),360.0f));
    applyOriginalViewLag(model,std::remainder(finite(state.pitchLag,0),360.0f),
                              std::remainder(finite(state.yawLag,0),360.0f));
    // Inventory selection is explicit. Unsupported shapes never masquerade as
    // empty hand; the block subset is reported by diagnostics below.
    const bool held=state.heldBlockIndex>=0;
    if(held) applyOriginalHeldBlock(model,state.swing,state.equip,fudgeX,fudgeY,0);
    else {
        applyOriginalEmptyHand(model,state.swing,state.equip,fudgeX,fudgeY,0);
        model.translate(originalfp::kArmPivot[0]*originalfp::kModelScale,
                        originalfp::kArmPivot[1]*originalfp::kModelScale,
                        originalfp::kArmPivot[2]*originalfp::kModelScale);
        model.rotate(originalfp::kRestZRadians*180/kPi,0,0,1);
        model.scale(originalfp::kModelScale,originalfp::kModelScale,originalfp::kModelScale);
    }
    Mesh mesh;
    std::array<Vec3,8> camera{};
    FirstPersonDiagnostics local;
    auto& d=diagnostics?*diagnostics:local;
    d.valid=true;d.aspect=aspect;d.fov=fov;d.modelView=model.m;d.projection=projection.m;
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
    } else if(state.heldBlockIndex<legacy::kBlockCount) {
        buildOriginalHeldCube(mesh,camera,projection,w,h,state.heldBlockIndex,d);
    }
    d.triangleCount=mesh.count;d.rasterBounds=meshBounds(mesh,w,h);
    if(pixels) draw(pixels,w,h,mesh);
}

void renderHeldItemOverlay(uint32_t* pixels,int w,int h,int itemId,int aux,float swing,float equip)
{
    if(!pixels || w<=0 || h<=0 || w>16384 || h>16384) return;
    // Do not mask aux, consult world state, infer block indices, or fall back to
    // an arbitrary damage-zero icon. The caller supplies a canonical identity.
    const std::string key="item:"+std::to_string(itemId)+":"+std::to_string(aux);
    const auto* icon=itemicons::findIcon(key.c_str());
    if(!icon) return;
    const originalfp::HeldSpriteSpec* spec=nullptr;
    for(const auto& candidate:originalfp::kHeldSprites)
        if(candidate.itemId==itemId && candidate.aux==aux) {spec=&candidate;break;}
    if(!spec) return;
    swing=std::isfinite(swing)?std::clamp(swing,0.0f,1.0f):0;
    equip=std::isfinite(equip)?std::clamp(equip,0.0f,1.0f):1;
    Matrix model;
    // isHandEquipped is audited class metadata, NOT a first-person pose switch:
    // the source only uses that switch in third-person PlayerRenderer. Tools
    // and materials share this branch; their actual icon geometry differs.
    applyOriginalHeldSprite(model,swing,equip,0,0,0,spec->mirrored);
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
               (icon->pixels[(size-1-y)*size+size-1-x]>>24)==255;
    };
    for(int y=0;y<size;++y) for(int x=0;x<size;++x) {
        const int u=size-1-x,v=size-1-y;
        if(!(icon->pixels[v*size+u]>>24)) continue;
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
                         u,v,u,v,icon->pixels,size,0xffffff,true);
    }
    draw(pixels,w,h,mesh);
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
