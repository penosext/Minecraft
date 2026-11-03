#include "MobRenderer.hpp"
#include "World.hpp"
#include "WorldRenderer.hpp"
#include "MobSkins.hpp"
#include "LegacyCatalog.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string_view>
#include <vector>

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kRadians = kPi / 180.0f, kNear = 0.08f;
struct Vec3 { float x, y, z; };
Vec3 subtract(Vec3 a, Vec3 b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
float dot(Vec3 a, Vec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
Vec3 cross(Vec3 a, Vec3 b)
{
    return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x};
}
struct Rotation {
    float cx, sx, cz, sz;
    Rotation(float x = 0, float z = 0)
        : cx(std::cos(x)), sx(std::sin(x)), cz(std::cos(z)), sz(std::sin(z)) {}
    Vec3 apply(Vec3 v) const
    {
        v = {v.x, cx*v.y-sx*v.z, sx*v.y+cx*v.z};
        return {cz*v.x-sz*v.y, sz*v.x+cz*v.y, v.z};
    }
};
struct Vertex { float x, y, q, uq, vq; };
struct ClipVertex { Vec3 p; float u, v; };
struct Triangle {
    Vertex a, b, c;
    const mobskins::Skin* skin;
    unsigned light;
    int u0, v0, u1, v1;
    bool hurt;
    size_t healthBar;
};
struct HealthBar {
    int left, top, width, filled;
    float depth;
    uint32_t color;
    bool visible = false;
};
constexpr size_t kNoHealthBar = std::numeric_limits<size_t>::max();
struct Camera {
    Vec3 eye, forward, right, up;
    float focal, screenX, screenY, daylight;
    Vec3 transform(Vec3 world) const
    {
        const Vec3 d = subtract(world, eye);
        // Camera Y down like ActorRenderer. Together with the model-to-world
        // axis conversion below, this preserves the original cube winding.
        return {dot(d,right), -dot(d,up), dot(d,forward)};
    }
    Vertex project(const ClipVertex& v) const
    {
        const float q = 1.0f / v.p.z;
        return {screenX+focal*v.p.x*q, screenY+focal*v.p.y*q,
                q, v.u*q, v.v*q};
    }
};
float edge(const Vertex& a, const Vertex& b, float x, float y)
{
    return (b.x-a.x)*(y-a.y)-(b.y-a.y)*(x-a.x);
}
struct Mesh {
    std::vector<Triangle>& triangles;
    const Camera& camera;
    const mcscene::Mob& mob;
    float cy, sy;
    size_t healthBar;
    Mesh(std::vector<Triangle>& t, const Camera& c, const mcscene::Mob& m, size_t bar)
        : triangles(t), camera(c), mob(m), cy(std::cos(std::remainder(m.yaw,360.0f)*kRadians)),
          sy(std::sin(std::remainder(m.yaw,360.0f)*kRadians)), healthBar(bar) {}
    Vec3 world(Vec3 p) const
    {
        // Original model feet are y=24, forward is -Z; 16 model pixels/block.
        // Rotate so scene yaw 0 points +Z, 90 points +X.
        const float x = -p.x/16, z = -p.z/16;
        return {mob.x+cy*x+sy*z, mob.y+(24-p.y)/16, mob.z-sy*x+cy*z};
    }
    void face(const std::array<Vec3,8>& points, const int* indices,
              float u0, float v0, float u1, float v1, const mobskins::Skin& skin,
              bool mirror)
    {
        std::array<ClipVertex,6> input{}, output{};
        const float uv[4][2] = {{u1,v0},{u0,v0},{u0,v1},{u1,v1}};
        for (int i=0;i<4;++i) input[i] = {points[indices[i]],uv[i][0],uv[i][1]};
        Vec3 normal = cross(subtract(input[1].p,input[0].p),subtract(input[2].p,input[0].p));
        if (mirror) normal = {-normal.x,-normal.y,-normal.z};
        if (dot(normal,input[0].p) >= 0) return;
        const float length2 = dot(normal,normal);
        if (length2 < 1e-14f) return;
        const float diffuse = std::max(0.0f,(-0.32f*normal.x-0.56f*normal.y-0.76f*normal.z)/std::sqrt(length2));
        const unsigned light = unsigned((0.28f+0.72f*camera.daylight)*(192+63*diffuse));
        // Sutherland-Hodgman clipping in camera space, including interpolated
        // UVs before reciprocal-depth projection (no triangles behind the eye).
        int count = 0;
        for (int i=0;i<4;++i) {
            const ClipVertex& a=input[i]; const ClipVertex& b=input[(i+1)%4];
            const bool insideA=a.p.z>=kNear, insideB=b.p.z>=kNear;
            if (insideA) output[count++]=a;
            if (insideA != insideB) {
                const float t=(kNear-a.p.z)/(b.p.z-a.p.z);
                output[count++]={{a.p.x+t*(b.p.x-a.p.x),a.p.y+t*(b.p.y-a.p.y),kNear},
                                  a.u+t*(b.u-a.u),a.v+t*(b.v-a.v)};
            }
        }
        if (count<3) return;
        const int left=int(std::min(u0,u1)), right=int(std::max(u0,u1))-1;
        const int top=int(std::min(v0,v1)), bottom=int(std::max(v0,v1))-1;
        if (left<0 || top<0 || right>=mobskins::kWidth || bottom>=mobskins::kHeight) return;
        for (int i=1;i+1<count;++i)
            triangles.push_back({camera.project(output[0]),camera.project(output[i]),camera.project(output[i+1]),
                                 &skin,light,left,top,right,bottom,
                                 std::isfinite(mob.hurtTime)&&mob.hurtTime>0,healthBar});
    }
    void cube(Vec3 origin, Vec3 size, Vec3 pivot, Rotation local, int u, int v,
              const mobskins::Skin& skin, bool mirror=false, float grow=0)
    {
        float x0=origin.x-grow, x1=origin.x+size.x+grow;
        if (mirror) std::swap(x0,x1);
        const float y0=origin.y-grow, y1=origin.y+size.y+grow;
        const float z0=origin.z-grow, z1=origin.z+size.z+grow;
        std::array<Vec3,8> points={{{x0,y0,z0},{x1,y0,z0},{x1,y1,z0},{x0,y1,z0},
                                  {x0,y0,z1},{x1,y0,z1},{x1,y1,z1},{x0,y1,z1}}};
        for (auto& p:points) {
            p=local.apply(p);
            p=camera.transform(world({p.x+pivot.x,p.y+pivot.y,p.z+pivot.z}));
        }
        // Original Cube.cpp 57-98 vertex order and six face atlas rectangles.
        static constexpr int faces[6][4]={{5,1,2,6},{0,4,7,3},{5,4,0,1},
                                          {2,3,7,6},{1,0,3,2},{4,5,6,7}};
        const float w=size.x,h=size.y,d=size.z;
        const float uv[6][4]={{u+d+w,v+d,u+d+w+d,v+d+h},
                              {float(u),v+d,u+d,v+d+h},{u+d,float(v),u+d+w,v+d},
                              {u+d+w,v+d,u+d+w+w,float(v)},
                              {u+d,v+d,u+d+w,v+d+h},{u+d+w+d,v+d,u+d+w+d+w,v+d+h}};
        for (int i=0;i<6;++i)
            face(points,faces[i],uv[i][0],uv[i][1],uv[i][2],uv[i][3],skin,mirror);
    }
    void model()
    {
        const float phase=std::isfinite(mob.walkPhase)?std::remainder(mob.walkPhase,2*kPi):0;
        const float stride=0.6f*std::sin(phase);
        const Rotation still;
        if (mob.kind==mcscene::MobKind::Chicken) {
            // Available original ChickenModel.cpp constructor, lines 8-39;
            // body xRot=pi/2 from setupAnim. Adult, no young scaling/head tracking.
            const auto& s=mobskins::kChicken;
            cube({-2,-6,-2},{4,6,3},{0,15,-4},still,0,0,s);
            cube({-2,-4,-4},{4,2,2},{0,15,-4},still,14,0,s);
            cube({-1,-2,-3},{2,2,2},{0,15,-4},still,14,4,s);
            cube({-3,-4,-3},{6,8,6},{0,16,0},Rotation(kPi/2),0,9,s);
            cube({-1,0,-3},{3,5,3},{-2,19,1},Rotation(stride),26,0,s);
            cube({-1,0,-3},{3,5,3},{1,19,1},Rotation(-stride),26,0,s);
            cube({0,0,-3},{1,4,6},{-4,13,0},still,24,13,s);
            cube({-1,0,-3},{1,4,6},{4,13,0},still,24,13,s);
            return;
        }
        if (mob.kind==mcscene::MobKind::Zombie) {
            // Classic humanoid reconstruction, not original HumanoidModel code.
            const auto& s=mobskins::kZombie;
            cube({-4,-8,-4},{8,8,8},{0,0,0},still,0,0,s);
            cube({-4,0,-2},{8,12,4},{0,0,0},still,16,16,s);
            cube({-3,-2,-2},{4,12,4},{-5,2,0},Rotation(-kPi/2+stride*0.1f),40,16,s);
            cube({-1,-2,-2},{4,12,4},{5,2,0},Rotation(-kPi/2-stride*0.1f),40,16,s,true);
            cube({-2,0,-2},{4,12,4},{-2,12,0},Rotation(stride),0,16,s);
            cube({-2,0,-2},{4,12,4},{2,12,0},Rotation(-stride),0,16,s,true);
            return;
        }
        const bool cow=mob.kind==mcscene::MobKind::Cow;
        const bool sheep=mob.kind==mcscene::MobKind::Sheep;
        const auto& s=cow?mobskins::kCow:(sheep?mobskins::kSheep:mobskins::kPig);
        const float leg=cow?12:(sheep?12:6);
        if (cow) {
            // Available original CowModel.cpp cuboids including horns and udder.
            cube({-4,-4,-6},{8,8,6},{0,4,-8},still,0,0,s);
            cube({-5,-5,-4},{1,3,1},{0,4,-8},still,22,0,s);
            cube({4,-5,-4},{1,3,1},{0,4,-8},still,22,0,s);
            cube({-6,-10,-7},{12,18,10},{0,5,2},Rotation(kPi/2),18,4,s);
            cube({-2,2,-8},{4,6,1},{0,5,2},Rotation(kPi/2),52,0,s);
        } else if (sheep) {
            // Classic sheep reconstruction plus enlarged original sheep_fur skin.
            cube({-3,-4,-6},{6,6,8},{0,6,-8},still,0,0,s);
            cube({-4,-10,-7},{8,16,6},{0,5,2},Rotation(kPi/2),28,8,s);
            cube({-3,-4,-4},{6,6,6},{0,6,-8},still,0,0,mobskins::kSheepFur,false,0.6f);
            cube({-4,-10,-7},{8,16,6},{0,5,2},Rotation(kPi/2),28,8,mobskins::kSheepFur,false,1.75f);
        } else {
            // Classic pig/Quadruped approximation: stock cube proportions and UVs.
            cube({-4,-4,-8},{8,8,8},{0,12,-6},still,0,0,s);
            cube({-2,0,-9},{4,3,1},{0,12,-6},still,16,16,s);
            cube({-5,-10,-7},{10,16,8},{0,11,2},Rotation(kPi/2),28,8,s);
        }
        const float x=cow?4:3;
        for (int i=0;i<4;++i) {
            const float px=(i&1)?x:-x, pz=i<2?7:(cow?-6:-5);
            const float angle=(i==0||i==3)?stride:-stride;
            cube({-2,0,-2},{4,leg,4},{px,24-leg,pz},Rotation(angle),0,16,s);
            if (sheep) cube({-2,0,-2},{4,6,4},{px,12,pz},Rotation(angle),0,16,mobskins::kSheepFur,false,0.5f);
        }
    }
};
const std::array<bool,legacy::kBlockCount+1>& opaqueBlocks()
{
    static const auto values=[] {
        std::array<bool,legacy::kBlockCount+1> result{};
        for (int i=0;i<legacy::kBlockCount;++i) {
            const auto& b=legacy::kBlockSpecs[i];
            const std::string_view key(b.key),shape(b.shape);
            // Match only fully opaque solid terrain; no false walls at water,
            // glass, leaf/plant cutouts. Partial shapes are conservatively skipped.
            result[i+1]=b.alpha>=0.99f && (shape=="cube"||shape=="grass") &&
                key!="glass" && key!="ice" && key!="water" && key!="calmWater" &&
                key.find("leaves")==std::string_view::npos;
        }
        return result;
    }();
    return values;
}
struct Axis { int step; float delta, next; };
Axis axis(float d, int cell, float origin)
{
    if (std::abs(d)<1e-7f) return {1,1e20f,1e20f};
    const int step=d<0?-1:1;
    const float delta=std::abs(1/d);
    return {step,delta,(step>0?cell+1-origin:origin-cell)*delta};
}
float worldDepth(const mcworld::World& world,const Camera& camera,float vx,float vy,float limit)
{
    const Vec3 d={camera.forward.x+camera.right.x*vx+camera.up.x*vy,
                  camera.forward.y+camera.up.y*vy,
                  camera.forward.z+camera.right.z*vx+camera.up.z*vy};
    int x=int(std::floor(camera.eye.x)),y=int(std::floor(camera.eye.y)),z=int(std::floor(camera.eye.z));
    Axis ax=axis(d.x,x,camera.eye.x),ay=axis(d.y,y,camera.eye.y),az=axis(d.z,z,camera.eye.z);
    const auto& opaque=opaqueBlocks();
    const auto blocks=[&] {
        const unsigned id=world.blockAt(x,y,z);
        return id<opaque.size() && opaque[id];
    };
    if (blocks()) return 0;
    for (int i=0;i<128;++i) {
        float t;
        if (ax.next<ay.next && ax.next<az.next) { t=ax.next; ax.next+=ax.delta; x+=ax.step; }
        else if (ay.next<az.next) { t=ay.next; ay.next+=ay.delta; y+=ay.step; }
        else { t=az.next; az.next+=az.delta; z+=az.step; }
        if (t>limit) break;
        if (blocks()) return t;
    }
    return 1e20f;
}
uint32_t shade(uint32_t pixel,unsigned light,bool hurt)
{
    unsigned r=((pixel>>16)&255)*light/255;
    unsigned g=((pixel>>8)&255)*light/255,b=(pixel&255)*light/255;
    // Platform approximation of the original red hurt overlay (red, alpha .3).
    // Apply to shaded skin texels, preserving texture alpha and all depth tests.
    if (hurt) { r=(r*7+255*3)/10; g=g*7/10; b=b*7/10; }
    return (pixel&0xff000000u)|(r<<16)|(g<<8)|b;
}
size_t healthBarFor(std::vector<HealthBar>& bars,const Camera& camera,const mcscene::Mob& mob,
                    int w,int h,float distance2)
{
    if (!(mob.health>0&&mob.maxHealth>0)||!std::isfinite(mob.health)||
        !std::isfinite(mob.maxHealth)||distance2>16*16) return kNoHealthBar;
    // Above each model's highest cuboid, including cow horns and sheep fleece.
    const float height=mob.kind==mcscene::MobKind::Zombie?2.0f:
        (mob.kind==mcscene::MobKind::Cow?1.5625f:(mob.kind==mcscene::MobKind::Sheep?1.3f:
        (mob.kind==mcscene::MobKind::Chicken?0.9375f:1.0f)));
    const Vec3 center=camera.transform({mob.x,mob.y+height+0.2f,mob.z});
    if (center.z<kNear) return kNoHealthBar;
    const Vertex p=camera.project({center,0,0});
    const int width=int(std::clamp(camera.focal/std::max(1.0f,std::sqrt(distance2)),24.0f,56.0f));
    // Reject offscreen floats before converting to int; close projections may be huge.
    if (p.x<0||p.x>=float(w)||p.y<0||p.y>=float(h)) return kNoHealthBar;
    const int left=int(std::floor(p.x))-width/2;
    int top=int(std::floor(p.y))-3;
    // Lift an overlapping bar just above the reticle; never clamp an offscreen
    // mob's bar back onto the HUD. No text/font or touch-UI dependency.
    if (left<=w/2+5&&left+width>w/2-5&&top<=h/2+5&&top+6>h/2-5) top=h/2-11;
    if (left<0||left+width>w||top<0||top+6>h) return kNoHealthBar;
    const float ratio=std::min(mob.health/mob.maxHealth,1.0f);
    const uint32_t color=ratio>0.5f?0xff40cc50u:(ratio>0.25f?0xffffcf40u:0xffe54040u);
    bars.push_back({left,top,width,std::clamp(int((width-2)*ratio+0.5f),1,width-2),center.z,color});
    return bars.size()-1;
}
uint32_t blackOver(uint32_t pixel)
{
    // Straight ARGB source-over for 0xa0000000, including nonopaque destinations.
    const unsigned a255=160*255+(pixel>>24)*95;
    uint32_t result=((a255+127)/255)<<24;
    for (int shift:{0,8,16})
        result|=(((((pixel>>shift)&255)*(pixel>>24)*95+a255/2)/a255)<<shift);
    return result;
}
void drawHealthBars(uint32_t* pixels,int w,const Camera& camera,const mcworld::World& world,
                    std::vector<HealthBar>& bars)
{
    // Far first so overlapping HUD bars are deterministic and the nearer wins.
    std::sort(bars.begin(),bars.end(),[](const HealthBar& a,const HealthBar& b) {
        if (a.depth!=b.depth) return a.depth>b.depth;
        if (a.left!=b.left) return a.left<b.left;
        if (a.top!=b.top) return a.top<b.top;
        if (a.filled!=b.filled) return a.filled<b.filled;
        return a.color<b.color;
    });
    for (const auto& bar:bars) {
        if (!bar.visible) continue;
        // Exact, untiled center ray: a wall hides the WHOLE HUD bar, never just
        // some fill pixels. Also reject near walls cutting across its rectangle.
        const float cx=bar.left+bar.width*0.5f,cy=bar.top+3.0f;
        const auto clear=[&](float x,float y) {
            return worldDepth(world,camera,(x-camera.screenX)/camera.focal,
                              (camera.screenY-y)/camera.focal,bar.depth+0.002f)>bar.depth+0.002f;
        };
        if (!clear(cx,cy)||!clear(bar.left+0.5f,bar.top+0.5f)||
            !clear(bar.left+bar.width-0.5f,bar.top+0.5f)||
            !clear(bar.left+0.5f,bar.top+5.5f)||
            !clear(bar.left+bar.width-0.5f,bar.top+5.5f)) continue;
        for (int y=0;y<6;++y) for (int x=0;x<bar.width;++x) {
            auto& pixel=pixels[size_t(bar.top+y)*w+bar.left+x];
            if (y>0&&y<5&&x>0&&x<=bar.filled) pixel=bar.color;
            else pixel=blackOver(pixel);
        }
    }
}
void draw(uint32_t* pixels,int w,int h,const Camera& camera,const mcworld::World& world,
          const std::vector<Triangle>& triangles,std::vector<HealthBar>& bars)
{
    if (triangles.empty()) return;
    float minX=float(w),minY=float(h),maxX=0,maxY=0,maxDepth=0;
    for (const auto& t:triangles) for (const auto& v:{t.a,t.b,t.c}) {
        minX=std::min(minX,v.x); maxX=std::max(maxX,v.x);
        minY=std::min(minY,v.y); maxY=std::max(maxY,v.y);
        maxDepth=std::max(maxDepth,1/v.q);
    }
    // Clamp as floats before int conversion (near-plane projection can be huge).
    const int left=int(std::clamp(std::floor(minX),0.0f,float(w)));
    const int top=int(std::clamp(std::floor(minY),0.0f,float(h)));
    const int right=int(std::clamp(std::ceil(maxX),0.0f,float(w)));
    const int bottom=int(std::clamp(std::ceil(maxY),0.0f,float(h)));
    if (right<=left||bottom<=top) return;
    const int stride=right-left,rows=bottom-top;
    // Keep approximately constant angular occlusion precision at high DPI.
    const int tile=std::max(4,(w+239)/240);
    thread_local std::vector<float> depth,terrain;
    thread_local std::vector<size_t> owners;
    depth.assign(size_t(stride)*rows,0);
    if (!bars.empty()) owners.assign(depth.size(),kNoHealthBar);
    const int tileStride=(stride+tile-1)/tile;
    terrain.assign(size_t(tileStride)*((rows+tile-1)/tile),-1);
    for (Triangle tri:triangles) {
        float area=edge(tri.a,tri.b,tri.c.x,tri.c.y);
        if (std::abs(area)<1e-7f) continue;
        if (area<0) { std::swap(tri.b,tri.c); area=-area; }
        const int x0=int(std::clamp(std::floor(std::min({tri.a.x,tri.b.x,tri.c.x})),float(left),float(right)));
        const int x1=int(std::clamp(std::ceil(std::max({tri.a.x,tri.b.x,tri.c.x})),float(left),float(right)));
        const int y0=int(std::clamp(std::floor(std::min({tri.a.y,tri.b.y,tri.c.y})),float(top),float(bottom)));
        const int y1=int(std::clamp(std::ceil(std::max({tri.a.y,tri.b.y,tri.c.y})),float(top),float(bottom)));
        if (x1<=x0||y1<=y0) continue;
        std::array<uint32_t,256> texels{};
        const int texStride=tri.u1-tri.u0+1;
        for (int v=tri.v0;v<=tri.v1;++v) for (int u=tri.u0;u<=tri.u1;++u)
            texels[size_t(v-tri.v0)*texStride+u-tri.u0]=shade(tri.skin->pixels[v*mobskins::kWidth+u],tri.light,tri.hurt);
        const float invArea=1/area;
        const float ax=-(tri.c.y-tri.b.y),ay=tri.c.x-tri.b.x;
        const float bx=-(tri.a.y-tri.c.y),by=tri.a.x-tri.c.x;
        const float cx=-(tri.b.y-tri.a.y),cy=tri.b.x-tri.a.x;
        float waRow=edge(tri.b,tri.c,x0+0.5f,y0+0.5f);
        float wbRow=edge(tri.c,tri.a,x0+0.5f,y0+0.5f);
        float wcRow=edge(tri.a,tri.b,x0+0.5f,y0+0.5f);
        const float dq=(ax*tri.a.q+bx*tri.b.q+cx*tri.c.q)*invArea;
        const float du=(ax*tri.a.uq+bx*tri.b.uq+cx*tri.c.uq)*invArea;
        const float dv=(ax*tri.a.vq+bx*tri.b.vq+cx*tri.c.vq)*invArea;
        for (int y=y0;y<y1;++y) {
            float wa=waRow,wb=wbRow,wc=wcRow;
            float q=(wa*tri.a.q+wb*tri.b.q+wc*tri.c.q)*invArea;
            float uq=(wa*tri.a.uq+wb*tri.b.uq+wc*tri.c.uq)*invArea;
            float vq=(wa*tri.a.vq+wb*tri.b.vq+wc*tri.c.vq)*invArea;
            size_t zi=size_t(y-top)*stride+x0-left,pi=size_t(y)*w+x0;
            for (int x=x0;x<x1;++x,++zi,++pi) {
                if (wa>=0 && wb>=0 && wc>=0 && q>depth[zi]) {
                    const float z=1/q;
                    const int u=std::clamp(int(uq*z),tri.u0,tri.u1),v=std::clamp(int(vq*z),tri.v0,tri.v1);
                    const uint32_t texel=texels[size_t(v-tri.v0)*texStride+u-tri.u0];
                    if ((texel>>24)>=128) {
                        const int tx=(x-left)/tile,ty=(y-top)/tile;
                        float& terrainZ=terrain[size_t(ty)*tileStride+tx];
                        if (terrainZ<0) {
                            const float px=std::min(float(w)-0.5f,float(left+tx*tile)+tile*0.5f);
                            const float py=std::min(float(h)-0.5f,float(top+ty*tile)+tile*0.5f);
                            terrainZ=worldDepth(world,camera,(px-camera.screenX)/camera.focal,
                                               (camera.screenY-py)/camera.focal,maxDepth+0.1f);
                        }
                        if (z<terrainZ-0.002f) {
                            pixels[pi]=texel|0xff000000u; depth[zi]=q;
                            if (!bars.empty()) owners[zi]=tri.healthBar;
                        }
                    }
                }
                wa+=ax; wb+=bx; wc+=cx; q+=dq; uq+=du; vq+=dv;
            }
            waRow+=ay; wbRow+=by; wcRow+=cy;
        }
    }
    // Only final depth winners count: a fully hidden mob cannot leave a floating
    // bar even when it happened to be submitted before its occluder.
    if (!bars.empty()) for (size_t owner:owners)
        if (owner!=kNoHealthBar) bars[owner].visible=true;
    drawHealthBars(pixels,w,camera,world,bars);
}
} // namespace

void renderMobOverlay(uint32_t* pixels,int w,int h,const mcworld::World& world,
                      float yaw,float pitch,const std::vector<mcscene::Mob>& mobs,float daylight)
{
    if (!pixels||w<=0||h<=0||w>16384||h>16384||!world.loaded()||mobs.empty()||
        !std::isfinite(yaw)||!std::isfinite(pitch)) return;
    yaw=std::remainder(yaw,360.0f)*kRadians;
    pitch=std::clamp(pitch,-80.0f,80.0f)*kRadians;
    if (!std::isfinite(daylight)) daylight=1;
    daylight=std::clamp(daylight,0.0f,1.0f);
    const float cy=std::cos(yaw),sy=std::sin(yaw),cp=std::cos(pitch),sp=std::sin(pitch);
    // WorldRenderer rays use x+1,y+1. Pixel-center rasterization therefore has
    // the optical center half a pixel left/up of the usual image center.
    const Camera camera{{world.playerX(),world.playerY()+1.62f,world.playerZ()},
                         {sy*cp,sp,cy*cp},{cy,0,-sy},{-sy*sp,cp,-cy*sp},
                         worldrender::focalLength(w,h),w*0.5f-0.5f,h*0.5f-0.5f,daylight};
    thread_local std::vector<Triangle> triangles;
    thread_local std::vector<HealthBar> bars;
    triangles.clear(); bars.clear();
    for (const auto& mob:mobs) {
        if (!std::isfinite(mob.x)||!std::isfinite(mob.y)||!std::isfinite(mob.z)||!std::isfinite(mob.yaw)||
            unsigned(mob.kind)>unsigned(mcscene::MobKind::Zombie)) continue;
        const Vec3 d=subtract({mob.x,mob.y,mob.z},camera.eye);
        if (dot(d,d)>32*32 || dot(d,camera.forward)<-2) continue;
        Mesh mesh(triangles,camera,mob,healthBarFor(bars,camera,mob,w,h,dot(d,d)));
        mesh.model();
    }
    draw(pixels,w,h,camera,world,triangles,bars);
}
