#include "MiningEffects.hpp"

#include "GameActions.hpp"
#include "World.hpp"
#include "LegacyCatalog.hpp"
#include "generatedBreakTextures.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace mcfx {
namespace {
constexpr float Pi = 3.14159265358979323846f;
constexpr float NearPlane = 0.05f;
constexpr float CrackOffset = 0.0015f;
struct V3 { float x, y, z; };
V3 add(V3 a, V3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
V3 sub(V3 a, V3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
V3 mul(V3 a, float s) { return {a.x*s,a.y*s,a.z*s}; }
float dot(V3 a,V3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
V3 cross(V3 a,V3 b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
float length(V3 a) { return std::sqrt(dot(a,a)); }
V3 normalized(V3 a) { const float n=length(a); return n>0?mul(a,1/n):V3{0,0,1}; }
struct Camera {
    V3 eye, forward, right, up;
    float focal, cx, cy;
    V3 toCamera(V3 p) const { const V3 d=sub(p,eye);return {dot(d,right),dot(d,up),dot(d,forward)}; }
};
Camera makeCamera(const mcworld::World& w,int width,int height,float yawDeg,float pitchDeg) {
    const float yaw=std::remainder(yawDeg,360.0f)*(Pi/180.0f);
    const float pitch=std::remainder(pitchDeg,360.0f)*(Pi/180.0f);
    const V3 f=normalized({std::sin(yaw)*std::cos(pitch),std::sin(pitch),std::cos(yaw)*std::cos(pitch)});
    const V3 r=normalized(cross({0,1,0},f));
    return {{w.playerX(),w.playerY()+1.62f,w.playerZ()},f,r,normalized(cross(f,r)),
            height/(2.0f*std::tan(35.0f*Pi/180.0f)),width*0.5f-0.5f,height*0.5f-0.5f};
}
int textureIndex(const wchar_t* name) {
    if(!name)return -1;
    for(std::size_t i=0;i<sizeof(textures::Names)/sizeof(textures::Names[0]);++i)
        if(std::wcscmp(name,textures::Names[i])==0)return int(i);
    return -1;
}
uint32_t sample(int texture,float u,float v) {
    if(texture<0)return 0;
    const int x=std::clamp(int(u*16),0,15), y=std::clamp(int(v*16),0,15);
    return textures::Pixels[texture][y*16+x];
}
uint32_t mixPixel(uint32_t dst,uint32_t src,float alpha) {
    const float a=std::clamp(alpha,0.0f,1.0f);
    const unsigned r=unsigned(((dst>>16)&255)*(1-a)+((src>>16)&255)*a);
    const unsigned g=unsigned(((dst>>8)&255)*(1-a)+((src>>8)&255)*a);
    const unsigned b=unsigned((dst&255)*(1-a)+(src&255)*a);
    return (dst&0xff000000u)|(r<<16)|(g<<8)|b;
}
struct RayHit { int x,y,z,face;float t;bool hit; };
RayHit trace(const mcworld::World& world,V3 origin,V3 direction,float limit) {
    int cell[3]={int(std::floor(origin.x)),int(std::floor(origin.y)),int(std::floor(origin.z))};
    const float o[3]={origin.x,origin.y,origin.z},d[3]={direction.x,direction.y,direction.z};
    int step[3];float next[3],delta[3];
    for(int a=0;a<3;++a){step[a]=d[a]>0?1:d[a]<0?-1:0;if(!step[a])next[a]=delta[a]=std::numeric_limits<float>::infinity();else{delta[a]=std::abs(1/d[a]);next[a]=((cell[a]+(step[a]>0?1:0))-o[a])/d[a];}}
    int face=-1;float entered=0;
    for(int iter=0;iter<256&&entered<=limit;++iter){
        if(cell[0]<0||cell[0]>=world.width()||cell[1]<0||cell[1]>=world.height()||cell[2]<0||cell[2]>=world.depth()||!world.chunkLoaded(cell[0],cell[2]))return {};
        const uint16_t id=world.blockAt(cell[0],cell[1],cell[2]);
        if(id&&id<=legacy::kBlockCount){const char* shape=legacy::kBlockSpecs[id-1].shape;if(std::strcmp(shape,"air")&&std::strcmp(shape,"liquid"))return {cell[0],cell[1],cell[2],face,entered,true};}
        const float dist=std::min({next[0],next[1],next[2]});if(!std::isfinite(dist)||dist>limit)return {};
        bool chose=false;for(int a=0;a<3;++a)if(next[a]<=dist+1e-6f){if(!chose){face=a==0?(step[a]>0?4:5):a==1?(step[a]>0?0:1):(step[a]>0?2:3);chose=true;}cell[a]+=step[a];next[a]+=delta[a];}entered=dist;
    }
    return {};
}
void faceUV(int face,V3 p,int bx,int by,int bz,float& u,float& v) {
    float a=0,b=0;switch(face){
        case 0: case 1:a=p.x-bx;b=p.z-bz;break;
        case 2: case 3:a=p.x-bx;b=p.y-by;break;
        default:a=p.z-bz;b=p.y-by;break;
    }
    u=std::clamp(a,0.0f,0.999999f);v=std::clamp(1.0f-b,0.0f,0.999999f);
}
bool crackSupported(uint16_t id) {
    return id&&id<=legacy::kBlockCount&&(!std::strcmp(legacy::kBlockSpecs[id-1].shape,"cube")||!std::strcmp(legacy::kBlockSpecs[id-1].shape,"grass"));
}
}

bool MiningEffects::bind(const mcworld::World& world) {
    if(!world.loaded()) { reset(); return false; }
    const bool changed=world_!=&world||path_!=world.path()||seed_!=world.seed()||dimension_!=world.currentDimension()||
        width_!=world.width()||height_!=world.height()||depth_!=world.depth();
    if(changed){reset();world_=&world;path_=world.path();seed_=world.seed();dimension_=world.currentDimension();width_=world.width();height_=world.height();depth_=world.depth();random_=seed_^0x9e3779b9u;if(!random_)random_=1;}
    return changed;
}
void MiningEffects::reset(){particles_={};world_=nullptr;path_.clear();seed_=0;dimension_=-1;width_=height_=depth_=0;seenEdit_=consumedBreak_=0;x_=y_=z_=0;stage_=-1;block_=0;nextParticle_=0;random_=0x6d2b79f5u;}
std::size_t MiningEffects::particleCount() const {std::size_t n=0;for(const auto&p:particles_)if(p.life>0)++n;return n;}
void MiningEffects::update(const mcworld::World& world,float dt,const mcgame::MiningState& mining) {
    bind(world);
    if(!std::isfinite(dt)||dt<0)dt=0;
    dt=std::min(dt,0.1f);
    for(auto& p:particles_)if(p.life>0){p.life=std::max(0.0f,p.life-dt);p.vy-=9.8f*dt;p.x+=p.vx*dt;p.y+=p.vy*dt;p.z+=p.vz*dt;}
    const auto edit=world.lastEdit();if(edit.revision>seenEdit_)seenEdit_=edit.revision;
    if(mining.broken&&edit.revision&&edit.revision!=consumedBreak_){
        consumedBreak_=edit.revision;const int id=int(world.lastMinedBlock())+1;
        if(id>0&&id<=legacy::kBlockCount){const auto& spec=legacy::kBlockSpecs[id-1];int tex=-1;
            for(const wchar_t* name:spec.faces){tex=textureIndex(name);if(tex>=0)break;}
            // This source has no exact per-tile atlas-coordinate table for every
            // legacy face. Crack textures are exact; fragment atlas UV is only
            // emitted when a face can be resolved in the verified source map.
            if(tex>=0){for(int i=0;i<64;++i){random_=random_*1664525u+1013904223u;const float rx=float(random_&65535)/65535.0f;random_=random_*1664525u+1013904223u;const float ry=float(random_&65535)/65535.0f;random_=random_*1664525u+1013904223u;const float rz=float(random_&65535)/65535.0f;random_=random_*1664525u+1013904223u;const float vx=(float(random_&65535)/32767.5f-1)*1.5f;random_=random_*1664525u+1013904223u;const float vz=(float(random_&65535)/32767.5f-1)*1.5f;
                    auto& p=particles_[nextParticle_++%MaxParticles];p={edit.x+rx,edit.y+ry,edit.z+rz,vx,1.4f+(float(random_&255)/255),vz,0.55f,0.55f,0.14f,uint16_t(tex),uint8_t((i*7)%4),uint8_t((i*11)%4)};}}
        }
    }
    if(mining.active&&std::isfinite(mining.progress)&&mining.progress>=0&&mining.progress<=1){
    if(x_!=mining.x||y_!=mining.y||z_!=mining.z){x_=mining.x;y_=mining.y;z_=mining.z;block_=world.blockAt(x_,y_,z_);}
    // Stage 0 is the "just started" crack. Allow it so the player sees
    // immediate feedback even before progress climbs above 0.1.
    const int stage=std::clamp(int(mining.progress*10.0f),0,9);stage_=stage;
}else stage_=-1;
}
// Screen rectangle covered by block (bx,by,bz). Edges crossing the near plane
// are clipped so a block touching the camera still yields its visible part.
bool blockScreenBounds(const Camera& cam,int bx,int by,int bz,int w,int h,int& x0,int& y0,int& x1,int& y1) {
    V3 c[8];for(int i=0;i<8;++i)c[i]=cam.toCamera({float(bx+(i&1)),float(by+((i>>1)&1)),float(bz+((i>>2)&1))});
    float minX=1e30f,minY=1e30f,maxX=-1e30f,maxY=-1e30f;bool any=false;
    auto add=[&](V3 p){const float q=1/p.z,sx=cam.cx+cam.focal*p.x*q,sy=cam.cy-cam.focal*p.y*q;minX=std::min(minX,sx);maxX=std::max(maxX,sx);minY=std::min(minY,sy);maxY=std::max(maxY,sy);any=true;};
    for(int i=0;i<8;++i)if(c[i].z>=NearPlane)add(c[i]);
    for(int i=0;i<8;++i)for(int bit=1;bit<8;bit<<=1)if(!(i&bit)){const V3 a=c[i],b=c[i|bit];
        if((a.z<NearPlane)!=(b.z<NearPlane)){const float t=(NearPlane-a.z)/(b.z-a.z);add({a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,NearPlane});}}
    if(!any||!std::isfinite(minX)||!std::isfinite(maxX)||!std::isfinite(minY)||!std::isfinite(maxY))return false;
    x0=std::max(0,int(std::floor(minX))-1);y0=std::max(0,int(std::floor(minY))-1);
    x1=std::min(w-1,int(std::ceil(maxX))+1);y1=std::min(h-1,int(std::ceil(maxY))+1);
    return x0<=x1&&y0<=y1;
}
void MiningEffects::render(uint32_t* pixels,int w,int h,const mcworld::World& world,float yaw,float pitch) {
    if(!pixels||w<=0||h<=0||!std::isfinite(yaw)||!std::isfinite(pitch)||!world.loaded())return;
    bind(world);if(w>16384||h>16384||uint64_t(w)*uint64_t(h)>uint64_t(std::numeric_limits<int>::max()))return;
    const Camera cam=makeCamera(world,w,h,yaw,pitch);
    // Each pixel uses the same center-ray DDA semantics as World::pick. The hit
    // target gate (first block hit must be the mined one) keeps cracks off
    // blocks in front; every visible face of the block gets the crack, as in
    // the original destroy-stage overlay.
    if(stage_>=0&&block_&&world.blockAt(x_,y_,z_)==block_&&crackSupported(block_)){
        const auto center=world.pick(yaw,pitch,5.0f);
        if(center.hit&&center.x==x_&&center.y==y_&&center.z==z_&&center.face>=0){
            const int crack=textureIndex((std::wstring(L"destroy_")+std::to_wstring(stage_)).c_str());
            if(crack>=0){const float limit=center.distance+1.75f;int x0,y0,x1,y1;if(blockScreenBounds(cam,x_,y_,z_,w,h,x0,y0,x1,y1))for(int py=y0;py<=y1;++py)for(int px=x0;px<=x1;++px){const float sx=(px-cam.cx)/cam.focal,sy=(cam.cy-py)/cam.focal;const V3 ray=normalized(add(cam.forward,add(mul(cam.right,sx),mul(cam.up,sy))));const RayHit hit=trace(world,cam.eye,ray,limit);if(!hit.hit||hit.x!=x_||hit.y!=y_||hit.z!=z_)continue;
                    const V3 point=add(cam.eye,mul(ray,hit.t+CrackOffset));float u,v;faceUV(hit.face,point,x_,y_,z_,u,v);const uint32_t texel=sample(crack,u,v);const float alpha=float(texel>>24)/255.0f;if(alpha<=0.1f)continue;const std::size_t index=std::size_t(py)*w+px;pixels[index]=mixPixel(pixels[index],texel,std::min(0.75f,alpha*0.62f));}}
        }
    }
    for(const auto& p:particles_)if(p.life>0){const V3 c=cam.toCamera({p.x,p.y,p.z});if(c.z<NearPlane)continue;const float q=1/c.z,sx=cam.cx+cam.focal*c.x*q,sy=cam.cy-cam.focal*c.y*q,r=std::max(1.0f,cam.focal*p.size*q);const int left=std::max(0,int(sx-r)),right=std::min(w-1,int(sx+r)),top=std::max(0,int(sy-r)),bottom=std::min(h-1,int(sy+r));const float fade=std::clamp(p.life/p.lifetime,0.0f,1.0f);
        for(int py=top;py<=bottom;++py)for(int px=left;px<=right;++px){const float u=(px-(sx-r))/(2*r),v=(py-(sy-r))/(2*r);const uint32_t color=sample(p.texture,(p.u+u)/4.0f,(p.v+v)/4.0f);const float a=float(color>>24)/255.0f*fade*0.85f;if(a<0.04f)continue;const V3 ray=normalized(add(cam.forward,add(mul(cam.right,(px-cam.cx)/cam.focal),mul(cam.up,(cam.cy-py)/cam.focal))));const RayHit obstacle=trace(world,cam.eye,ray,c.z+0.03f);if(obstacle.hit&&obstacle.t<c.z-0.02f)continue;const size_t i=size_t(py)*w+px;pixels[i]=mixPixel(pixels[i],color,a);}}
}
} // namespace mcfx
