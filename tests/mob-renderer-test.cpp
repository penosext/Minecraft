#include "MobRenderer.hpp"
#include "MobSkins.hpp"
#include "World.hpp"
#include "LegacyCatalog.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// Standalone C++17. Optional args: existing image output dir, mob PNG source dir.
// No assert: guards and hash checks remain active with -DNDEBUG.
namespace {
constexpr uint32_t background=0xff24364au,canary=0xd35e79b1u;
constexpr float pi=3.14159265358979323846f;
using mcscene::Mob;
using mcscene::MobKind;
void check(bool c,const char* message) { if (!c) throw std::runtime_error(message); }
// Independent expectation from the original 70-degree vertical FOV, not w/2.
float focalLength(int height) { return height/(2*std::tan(35*pi/180)); }
int barWidth(float eyeDistance,int height=266)
{
    return int(std::clamp(focalLength(height)/std::max(1.0f,eyeDistance),24.0f,56.0f));
}
uint32_t rotr(uint32_t n,unsigned bits) { return (n>>bits)|(n<<(32-bits)); }
std::string sha256(std::vector<uint8_t> data)
{
    static constexpr uint32_t k[64]={
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    std::array<uint32_t,8> s={{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19}};
    const uint64_t bits=uint64_t(data.size())*8;
    data.push_back(0x80);
    while (data.size()%64!=56) data.push_back(0);
    for (int i=7;i>=0;--i) data.push_back(uint8_t(bits>>(i*8)));
    for (size_t offset=0;offset<data.size();offset+=64) {
        uint32_t w[64];
        for (int i=0;i<16;++i) { w[i]=0; for (int j=0;j<4;++j) w[i]=(w[i]<<8)|data[offset+i*4+j]; }
        for (int i=16;i<64;++i) w[i]=w[i-16]+(rotr(w[i-15],7)^rotr(w[i-15],18)^(w[i-15]>>3))+
            w[i-7]+(rotr(w[i-2],17)^rotr(w[i-2],19)^(w[i-2]>>10));
        auto v=s;
        for (int i=0;i<64;++i) {
            const uint32_t t1=v[7]+(rotr(v[4],6)^rotr(v[4],11)^rotr(v[4],25))+
                ((v[4]&v[5])^(~v[4]&v[6]))+k[i]+w[i];
            const uint32_t t2=(rotr(v[0],2)^rotr(v[0],13)^rotr(v[0],22))+
                ((v[0]&v[1])^(v[0]&v[2])^(v[1]&v[2]));
            v={{t1+t2,v[0],v[1],v[2],v[3]+t1,v[4],v[5],v[6]}};
        }
        for (int i=0;i<8;++i) s[i]+=v[i];
    }
    std::string result;
    for (auto word:s) for (int shift=28;shift>=0;shift-=4) result+="0123456789abcdef"[(word>>shift)&15];
    return result;
}
struct TemporaryRoot {
    char path[40]="/tmp/mc-mob-test.XXXXXX";
    TemporaryRoot() { check(mkdtemp(path)!=nullptr,"mkdtemp"); }
    ~TemporaryRoot() { std::error_code e; std::filesystem::remove_all(path,e); }
};
size_t cell(int x,int y,int z) { return (size_t(y)*mcworld::Depth+z)*mcworld::Width+x; }
uint16_t blockId(std::string_view key)
{
    for (int i=0;i<legacy::kBlockCount;++i) if (key==legacy::kBlockSpecs[i].key) return uint16_t(i+1);
    throw std::runtime_error("Unknown fixture block");
}
void hashes(const char* sourceDir)
{
    check(sha256({'a','b','c'})=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA-256 self test");
    check(mobskins::kWidth==64&&mobskins::kHeight==32,"Original mob dimensions");
    const char* pinned[]={
        "0ad7e2885302a58e28512451af7fb433ce17ee9ad47a051b326ef88520cc69c9",
        "5b901e770c7d567ac2e46382d2ec3e83bfcbcaaae024f7610b50299ae836773e",
        "a4c953073c2975e6d8035ba59af7bff84016ba7edfd5fcc5df24907066d1ed6a",
        "9f4ef6ad8906027ad6e5ebb9259f4107c449cfbfe703f641d02b0a785417964d",
        "f274296053d7993975270300388cd184635010a7082cf56c537e903274b12216",
        "c27e7c20555f1bf5ef1cd848a4291f90465000708185e6743e72b8adffed2a4b",
        "6bd21f2f5f873d5b2d70ea84451ee616a3e888cdfb0d90b1fb59681a65a156a1"};
    check(std::size(mobskins::kAll)==std::size(pinned),"Skin count");
    for (size_t i=0;i<std::size(mobskins::kAll);++i) {
        const auto& s=*mobskins::kAll[i];
        check(std::string(s.sourceSha256)==pinned[i],"Pinned original mob PNG hash");
        std::vector<uint8_t> rgba;
        for (size_t j=0;j<64*32;++j) for (int shift:{16,8,0,24}) rgba.push_back(uint8_t(s.pixels[j]>>shift));
        check(sha256(rgba)==s.rgbaSha256,"Embedded mob pixels differ from original RGBA");
        if (sourceDir) {
            std::ifstream file(std::string(sourceDir)+"/"+s.name+".png",std::ios::binary);
            check(bool(file),"Missing original mob PNG");
            check(sha256(std::vector<uint8_t>((std::istreambuf_iterator<char>(file)),{}))==s.sourceSha256,
                  "Actual mob PNG file hash");
        }
    }
}
std::vector<uint32_t> frame(const mcworld::World& world,const std::vector<Mob>& mobs,
                            int w=960,int h=266,float yaw=0,float pitch=0,float daylight=1)
{
    std::vector<uint32_t> p(size_t(w)*h,background);
    renderMobOverlay(p.data(),w,h,world,yaw,pitch,mobs,daylight);
    return p;
}
struct Bounds {
    int x0,y0,x1=-1,y1=-1;
    size_t count=0;
    double sx=0,sy=0,brightness=0;
    std::set<uint32_t> colors;
    Bounds(const std::vector<uint32_t>& p,int w,int h):x0(w),y0(h)
    {
        for (int y=0;y<h;++y) for (int x=0;x<w;++x) {
            const auto pixel=p[size_t(y)*w+x];
            if (pixel==background) continue;
            ++count; sx+=x; sy+=y;
            x0=std::min(x0,x); y0=std::min(y0,y); x1=std::max(x1,x); y1=std::max(y1,y);
            colors.insert(pixel); brightness+=((pixel>>16)&255)+((pixel>>8)&255)+(pixel&255);
            check((pixel>>24)==255,"Invalid rendered mob alpha");
        }
    }
};
void writePpm(const std::string& path,const std::vector<uint32_t>& p,int w,int h)
{
    std::ofstream f(path,std::ios::binary);
    check(bool(f),"Image output directory must exist");
    f<<"P6\n"<<w<<' '<<h<<"\n255\n";
    for (auto pixel:p) { const char rgb[]={char(pixel>>16),char(pixel>>8),char(pixel)}; f.write(rgb,3); }
    check(bool(f),"Write image");
}
void models(mcworld::World& world,const char* outputDir)
{
    const float x=world.playerX(),y=world.playerY(),z=world.playerZ();
    std::set<std::vector<uint32_t>> silhouettes;
    for (int i=0;i<5;++i) {
        Mob m{1,MobKind(i),x,y,z+6,155,0};
        const auto p=frame(world,{m}); const Bounds b(p,960,266);
        std::cout<<"model="<<i<<" bbox=["<<b.x0<<','<<b.y0<<".."<<b.x1<<','<<b.y1<<"] pixels="<<b.count<<" colors="<<b.colors.size()<<'\n';
        check(b.count>80&&b.count<30000,"Model missing or excessive coverage");
        check(b.colors.size()>8,"Must render actual multi-texel original skin, not flat boxes");
        check(b.x0>300&&b.x1<650,"World model projection should be near screen center");
        const float feetY=266/2.0f+focalLength(266)*1.62f/6;
        // Cuboid feet extend up to one block toward the eye; allow that depth
        // variation plus two pixels for the optical center/rasterization.
        const float feetTolerance=focalLength(266)*1.62f*(1/5.0f-1/6.0f)+2;
        check(std::abs(b.y1-feetY)<=feetTolerance,"Feet must match the 70-degree vertical-FOV projection");
        silhouettes.insert(p);
        m.yaw+=70; check(frame(world,{m})!=p,"Yaw must rotate geometry");
        m.yaw-=70; m.walkPhase=pi/2; check(frame(world,{m})!=p,"Walk must articulate legs");
        m.walkPhase=std::numeric_limits<float>::quiet_NaN(); check(frame(world,{m})==p,"NaN walk defaults idle");
        const auto night=frame(world,{m},960,266,0,0,0); const Bounds n(night,960,266);
        check(n.count==b.count&&n.brightness<b.brightness*0.5,"Daylight shading must preserve geometry");
        if (outputDir) writePpm(std::string(outputDir)+"/mob-"+std::to_string(i)+".ppm",p,960,266);
    }
    check(silhouettes.size()==5,"Five distinguishable mob models");
    Mob near{1,MobKind::Cow,x,y,z+4,180,0}; auto far=near; far.z=z+8;
    const Bounds a(frame(world,{near},960,540),960,540),b(frame(world,{far},960,540),960,540);
    check(a.count>b.count*3,"Perspective scale should shrink with world distance");
    auto side=near; side.x=x+1;
    const Bounds c(frame(world,{side},960,540),960,540);
    check(c.sx/c.count>a.sx/a.count+70,"Rightward world displacement projection");
    auto raised=near; raised.y+=0.5f;
    const Bounds d(frame(world,{raised},960,540),960,540);
    check(d.sy/d.count<a.sy/a.count-20,"World height projects upward");
    auto right=near; right.x=x+4; right.z=z;
    check(frame(world,{right},960,266,90)!=std::vector<uint32_t>(960*266,background),"Yaw 90 faces +X");
    auto same=near; same.z=z+6;
    const Bounds pitch(frame(world,{same},960,266,0,10),960,266);
    const Bounds level(frame(world,{same}),960,266);
    const double pitchShift=pitch.sy/pitch.count-level.sy/level.count;
    // At the horizon a 10-degree upward pitch shifts geometry down by f*tan(10).
    // Allow 10% for the model's extent/depth and two pixels for rasterization.
    const float expectedPitchShift=focalLength(266)*std::tan(10*pi/180);
    check(std::abs(pitchShift-expectedPitchShift)<=expectedPitchShift*.1f+2,
          "Positive camera pitch must match the focal-length projection");
}
void occlusion(mcworld::World& world,const char* outputDir)
{
    const float x=world.playerX(),y=world.playerY(),z=world.playerZ();
    Mob front{1,MobKind::Pig,x,y,z+5,180,0};
    Mob back{2,MobKind::Zombie,x,y,z+7,180,0};
    const auto single=frame(world,{front}); const auto pair=frame(world,{front,back});
    check(pair==frame(world,{back,front}),"Shared entity z-buffer must not depend on mob order");
    check(pair!=single,"Uncovered distant zombie head should remain visible");
    const auto farOnly=frame(world,{back});
    size_t overlap=0;
    for (size_t i=0;i<pair.size();++i) if (single[i]!=background&&farOnly[i]!=background) {
        ++overlap; check(pair[i]==single[i],"Near mob should hide farther mob at each pixel");
    }
    check(overlap>100,"Depth fixture must contain real overlapping geometry");
    auto* voxels=const_cast<uint16_t*>(world.blockData());
    const int wallZ=int(std::floor(z))+3;
    const auto wall=[&](uint16_t id,bool half) {
        for (int iy=std::max(0,int(y)-2);iy<std::min(mcworld::Height,int(y)+5);++iy)
            for (int ix=std::max(0,int(x)-4);ix<std::min(mcworld::Width,int(x)+5);++ix)
                voxels[cell(ix,iy,wallZ)]=half&&ix>=int(x)?0:id;
    };
    wall(blockId("stone"),false);
    check(Bounds(frame(world,{front,back}),960,266).count==0,"Solid world wall must hide mobs");
    // Put the model on the ray through the integer wall edge; spawn has a
    // fractional X, so centering it on playerX would miss the half-wall entirely.
    auto edgeMob=front;
    edgeMob.x=x+(std::floor(x)-x)*(front.z-z)/(float(wallZ)-z);
    wall(0,false);
    const auto edgeWhole=frame(world,{edgeMob});
    wall(blockId("stone"),true);
    const auto partial=frame(world,{edgeMob}); const Bounds clipped(partial,960,266),whole(edgeWhole,960,266);
    std::cout<<"wall partial="<<clipped.count<<" whole="<<whole.count<<" left="<<clipped.x0<<" original-left="<<whole.x0<<'\n';
    // Coverage scales with focal length squared; require a visible fraction,
    // not an absolute pixel count inherited from the wider old projection.
    check(clipped.count>whole.count*0.05&&clipped.count<whole.count*0.8,"World wall should occlude only covered pixels");
    check(clipped.x0>whole.x0+10,"Partial wall world depth should hide left half");
    if (outputDir) writePpm(std::string(outputDir)+"/wall-occlusion.ppm",partial,960,266);
    wall(blockId("water"),false);
    check(frame(world,{front})==single,"Water is not an opaque world wall");
    wall(65535,false); check(frame(world,{front})==single,"Invalid voxel ids are air");
    wall(0,false); check(frame(world,{front})==single,"Live removed wall must be visible next call");
}
struct Changes {
    int x0,y0,x1=-1,y1=-1;
    size_t count=0;
    Changes(const std::vector<uint32_t>& a,const std::vector<uint32_t>& b,int w,int h):x0(w),y0(h)
    {
        check(a.size()==b.size(),"Frame size mismatch");
        for (int y=0;y<h;++y) for (int x=0;x<w;++x) if (a[size_t(y)*w+x]!=b[size_t(y)*w+x]) {
            ++count; x0=std::min(x0,x); x1=std::max(x1,x); y0=std::min(y0,y); y1=std::max(y1,y);
        }
    }
    int width() const { return x1-x0+1; }
};
void combat(mcworld::World& world,const char* outputDir)
{
    constexpr int w=960,h=266;
    const float x=world.playerX(),y=world.playerY(),z=world.playerZ();
    const float nan=std::numeric_limits<float>::quiet_NaN(),inf=std::numeric_limits<float>::infinity();
    Mob currentMob{1,MobKind::Pig,x,y,z+6,180,0};
    const int expectedBarWidth=barWidth(std::sqrt(6*6+1.62f*1.62f),h);
    const int halfFill=int((expectedBarWidth-2)*.5f+.5f);
    const int quarterFill=int((expectedBarWidth-2)*.25f+.5f);
    const auto bare=frame(world,{currentMob});
    // HP defaults must not change any pre-combat aggregate image.
    for (int kind=0;kind<5;++kind) {
        auto m=currentMob; m.kind=MobKind(kind);
        const auto original=frame(world,{m});
        m.health=0; m.maxHealth=20;
        check(frame(world,{m})==original,"health=0 must retain legacy body and skip bar");
        m.health=10; m.maxHealth=0;
        check(frame(world,{m})==original,"maxHealth=0 must skip bar without hiding body");
        for (const auto& hp:{std::array<float,2>{nan,20},{10,nan},{inf,20},{10,inf},{-1,20},{10,-1}}) {
            m.health=hp[0]; m.maxHealth=hp[1];
            check(frame(world,{m})==original,"Invalid health must not draw a bar");
        }
        m.health=10; m.maxHealth=20;
        const auto active=frame(world,{m}); const Changes bar(active,original,w,h);
        check(bar.count==size_t(expectedBarWidth)*6&&bar.width()==expectedBarWidth&&bar.y1-bar.y0==5,
              "Each current mob type needs one complete overhead health bar");
        const Bounds model(original,w,h);
        check(bar.y1<model.y0,"Health bar must project above model head");
        check((active[size_t(bar.y0)*w+bar.x0]>>24)==255,"ARGB bar background must composite, not overwrite alpha");
        check(active[size_t(bar.y0)*w+bar.x0]!=0xff000000u,"Bar background must remain translucent");
        // Hurt changes only skin color; alpha rejection and pixel coverage stay identical.
        m.health=0; m.maxHealth=0; m.hurtTime=10;
        const auto hurt=frame(world,{m}); size_t tinted=0;
        for (size_t i=0;i<hurt.size();++i) {
            check((hurt[i]==background)==(original[i]==background),"Hurt tint changed skin coverage/depth");
            if (original[i]==background) continue;
            ++tinted;
            const unsigned r=(original[i]>>16)&255,g=(original[i]>>8)&255,b=original[i]&255;
            check(hurt[i]==(0xff000000u|(((r*7+255*3)/10)<<16)|((g*7/10)<<8)|(b*7/10)),
                  "Hurt tint must be red .3 over original shaded skin");
        }
        check(tinted>80,"Hurt fixture must contain visible skin");
        m.hurtTime=1; check(frame(world,{m})==hurt,"Hurt tint lasts until ticks reach zero");
        for (float ticks:{0.0f,-1.0f,nan,inf}) {
            m.hurtTime=ticks;
            check(frame(world,{m})==original,"Expired/invalid hurtTime must restore exact original image");
        }
    }
    // Independent maxima: same health on pig/chicken means 1/2 vs 1/4, not global HP.
    currentMob.health=5; currentMob.maxHealth=10;
    const auto half=frame(world,{currentMob}); const Changes hb(half,bare,w,h);
    check(half[size_t(hb.y0+1)*w+hb.x0+1]==0xffffcf40u,"Half health should be yellow");
    int yellow=0;
    for (int px=hb.x0+1;px<hb.x1;++px) if (half[size_t(hb.y0+1)*w+px]==0xffffcf40u) ++yellow;
    check(yellow==halfFill,"Half HP must fill half the bar interior");
    auto chicken=currentMob; chicken.kind=MobKind::Chicken; chicken.maxHealth=20;
    auto noChicken=chicken; noChicken.health=0;
    const auto quarter=frame(world,{chicken}); const Changes cb(quarter,frame(world,{noChicken}),w,h);
    int red=0;
    for (int px=cb.x0+1;px<cb.x1;++px) if (quarter[size_t(cb.y0+1)*w+px]==0xffe54040u) ++red;
    check(red==quarterFill,"Second type's independent max HP must produce quarter red fill");
    auto leftPig=currentMob; leftPig.x=x-1.5f;
    auto rightChicken=chicken; rightChicken.x=x+1.5f;
    const auto two=frame(world,{leftPig,rightChicken});
    check(two==frame(world,{rightChicken,leftPig}),"Two type HP bars must be independent of submission order");
    const int sideWidth=barWidth(std::sqrt(6*6+1.5f*1.5f+1.62f*1.62f),h);
    check(std::count(two.begin(),two.end(),0xffffcf40u)==int((sideWidth-2)*.5f+.5f)*4&&
          std::count(two.begin(),two.end(),0xffe54040u)==int((sideWidth-2)*.25f+.5f)*4,
          "Two simultaneously visible types must retain their own HP ratio/color");
    // Straight ARGB background is not opaque black or premultiplied output.
    for (uint32_t bg:{0x0024364au,0x8024364au}) {
        std::vector<uint32_t> p(size_t(w)*h,bg);
        renderMobOverlay(p.data(),w,h,world,0,0,{currentMob},1);
        const unsigned a255=160*255+(bg>>24)*95;
        uint32_t expected=((a255+127)/255)<<24;
        for (int shift:{0,8,16}) expected|=(((((bg>>shift)&255)*(bg>>24)*95+a255/2)/a255)<<shift);
        check(p[size_t(hb.y0)*w+hb.x0]==expected,"Health bar black background must use straight ARGB source-over");
    }
    auto full=currentMob; full.health=full.maxHealth;
    const auto fullFrame=frame(world,{full});
    check(fullFrame[size_t(hb.y0+1)*w+hb.x0+1]==0xff40cc50u,"Full HP should be green");
    full.health=1000; check(frame(world,{full})==fullFrame,"Overfull HP must clamp to max");
    full.health=std::numeric_limits<float>::max(); full.maxHealth=std::numeric_limits<float>::min();
    check(frame(world,{full})==fullFrame,"Extreme finite HP ratio must clamp before integer conversion");
    // Two heights exercise both clamp endpoints and intermediate focal/d widths.
    for (int height:{h,540}) for (float distance:{4.0f,12.0f,15.9f}) {
        auto m=currentMob; m.z=z+distance;
        auto no=m; no.health=0;
        const Changes bar(frame(world,{m},w,height),frame(world,{no},w,height),w,height);
        const float d=std::sqrt(distance*distance+1.62f*1.62f);
        const int expected=barWidth(d,height);
        check(bar.count==size_t(expected)*6&&bar.width()==expected,"Bar width must follow distance and clamp 24..56px");
    }
    auto edge=currentMob; edge.x=x+10; edge.z=z+10;
    auto noEdge=edge; noEdge.health=0;
    const Changes small(frame(world,{edge},320,266,45),frame(world,{noEdge},320,266,45),320,266);
    check(small.width()==24&&small.count==24*6,"Distant small viewport bar must clamp to 24px");
    for (float distance:{16.0f,17.0f,25.0f}) {
        auto m=currentMob; m.z=z+distance;
        auto no=m; no.health=0;
        check(frame(world,{m})==frame(world,{no}),"Eye distance >16 must skip health bars, retaining body");
    }
    // The 16-block limit includes its boundary (distance measured from eye).
    auto atLimit=currentMob; atLimit.y=y+1.62f; atLimit.z=z+16;
    auto noLimit=atLimit; noLimit.health=0;
    check(frame(world,{atLimit})!=frame(world,{noLimit}),"Exactly 16 blocks from eye must show bar");
    auto reticle=currentMob; reticle.y=y+0.42f;
    auto noReticle=reticle; noReticle.health=0;
    const auto r=frame(world,{reticle}),nr=frame(world,{noReticle});
    check(r!=nr,"Reticle-adjacent mob must retain a bar above crosshair");
    for (int py=h/2-4;py<=h/2+4;++py) for (int px=w/2-4;px<=w/2+4;++px)
        check(r[size_t(py)*w+px]==nr[size_t(py)*w+px],"Bar must not cover center crosshair");
    // A hidden entity cannot leave a floating bar regardless of submission order.
    auto near=currentMob; near.kind=MobKind::Zombie; near.z=z+4; near.health=0;
    auto hidden=currentMob; hidden.kind=MobKind::Chicken; hidden.z=z+7;
    check(frame(world,{near,hidden})==frame(world,{near}),"Fully entity-occluded mob must have no health bar");
    check(frame(world,{hidden,near})==frame(world,{near}),"Bar visibility must use final z-buffer winners");
    auto front=currentMob; front.z=z+5; front.hurtTime=10;
    auto back=currentMob; back.kind=MobKind::Zombie; back.z=z+7; back.hurtTime=10;
    const auto frontOnly=frame(world,{front}),pair=frame(world,{front,back});
    check(pair==frame(world,{back,front}),"Combat tint/bars must preserve entity depth order");
    auto frontBare=front; frontBare.health=0; frontBare.hurtTime=0;
    auto backBare=back; backBare.health=0; backBare.hurtTime=0;
    const auto fbody=frame(world,{frontBare}),bbody=frame(world,{backBare});
    size_t overlap=0;
    for (size_t i=0;i<pair.size();++i) if (fbody[i]!=background&&bbody[i]!=background) {
        ++overlap;
        check(pair[i]==frontOnly[i],"Near hurt skin must still occlude far body");
    }
    check(overlap>100,"Combat depth fixture needs real overlapping body pixels");
    auto* voxels=const_cast<uint16_t*>(world.blockData());
    const int wallZ=int(std::floor(z))+3;
    const auto wall=[&](uint16_t id) {
        for (int iy=std::max(0,int(y)-2);iy<std::min(mcworld::Height,int(y)+5);++iy)
            for (int ix=std::max(0,int(x)-4);ix<std::min(mcworld::Width,int(x)+5);++ix)
                voxels[cell(ix,iy,wallZ)]=id;
    };
    wall(blockId("stone"));
    auto hurt=currentMob; hurt.hurtTime=10;
    check(frame(world,{hurt})==std::vector<uint32_t>(w*h,background),"Wall fully occluded: no body/tint/bar through wall");
    wall(0);
    // A low wall blocks the bar ray while some of the model is still visible.
    const int wallY=int(std::floor(y+1.62f+(1.2f-1.62f)*(float(wallZ)-z)/6));
    for (int ix=std::max(0,int(x)-4);ix<std::min(mcworld::Width,int(x)+5);++ix)
        voxels[cell(ix,wallY,wallZ)]=blockId("stone");
    const auto occluded=frame(world,{currentMob}),noBar=frame(world,{Mob{1,MobKind::Pig,x,y,z+6,180,0}});
    check(Bounds(noBar,w,h).count>0,"Center-ray wall fixture must leave visible body pixels");
    check(occluded==noBar,"Blocked center ray must suppress the whole bar, not partial HUD pixels");
    wall(0);
    check(frame(world,{currentMob})==half,"Removing wall restores current mob bar without stale cache");
    std::vector<uint32_t> reused(w*h,background);
    renderMobOverlay(reused.data(),w,h,world,0,0,{hurt},1);
    std::fill(reused.begin(),reused.end(),background);
    renderMobOverlay(reused.data(),w,h,world,0,0,{},1);
    check(reused==std::vector<uint32_t>(w*h,background),"Scene-removed dead mob must leave no cached body/bar");
    if (outputDir) writePpm(std::string(outputDir)+"/combat-current-mob.ppm",frame(world,{hurt}),w,h);
}
void guards(const mcworld::World& world)
{
    const float x=world.playerX(),y=world.playerY(),z=world.playerZ();
    Mob mob{1,MobKind::Cow,x,y,z+5,180,0};
    const float nan=std::numeric_limits<float>::quiet_NaN(),inf=std::numeric_limits<float>::infinity();
    renderMobOverlay(nullptr,960,266,world,0,0,{mob},1);
    uint32_t pixel=canary; mcworld::World unloaded;
    renderMobOverlay(&pixel,1,1,unloaded,0,0,{mob},1);
    for (const auto& size:{std::array<int,2>{0,1},{1,0},{-1,20},{20,-1},{16385,1},{1,16385}})
        renderMobOverlay(&pixel,size[0],size[1],world,0,0,{mob},1);
    renderMobOverlay(&pixel,1,1,world,nan,0,{mob},1);
    renderMobOverlay(&pixel,1,1,world,0,inf,{mob},1);
    check(pixel==canary,"Invalid render inputs changed destination");
    const auto empty=std::vector<uint32_t>(960*266,background);
    check(frame(world,{})==empty,"Empty mob list");
    for (int type=0;type<8;++type) {
        auto m=mob;
        if (type==0) m.x=nan;
        if (type==1) m.y=inf;
        if (type==2) m.z=nan;
        if (type==3) m.yaw=nan;
        if (type==4) m.kind=MobKind(-1);
        if (type==5) m.z=z-5;
        if (type==6) m.z=z+33;
        if (type==7) m.x=1e30f;
        check(frame(world,{m})==empty,"Invalid/far/behind mob was rendered");
    }
    check(frame(world,{mob},960,266,0,0,nan)==frame(world,{mob}),"Nonfinite daylight defaults day");
    check(frame(world,{mob},960,266,0,0,-1)==frame(world,{mob},960,266,0,0,0),"Clamp dark daylight");
    check(frame(world,{mob},960,266,0,0,2)==frame(world,{mob}),"Clamp bright daylight");
    for (const auto& size:{std::array<int,2>{1,1},{1,513},{513,1},{2,2},{17,29},{266,960},{960,266},{1920,1080}}) {
        const int w=size[0],h=size[1];
        for (int kind=0;kind<5;++kind) for (float distance:{0.0f,0.1f,0.5f,5.0f}) {
            auto m=mob; m.kind=MobKind(kind); m.z=z+distance; m.yaw=37;
            std::vector<uint32_t> p(size_t(w)*h+2,canary);
            renderMobOverlay(p.data()+1,w,h,world,0,-10,{m},1);
            check(p.front()==canary&&p.back()==canary,"Mob buffer canary overwrite (including near clip)");
            m.health=3; m.maxHealth=20; m.hurtTime=10;
            for (float shift:{-3.0f,0.0f,3.0f}) {
                m.x=x+shift;
                renderMobOverlay(p.data()+1,w,h,world,0,-10,{m},1);
                check(p.front()==canary&&p.back()==canary,"Combat HUD/tint buffer canary overwrite");
            }
        }
    }
    // Exercise complete and clipped HUD rectangles at all four viewport edges.
    for (float dx:{-6.1f,-5.8f,0.0f,5.8f,6.1f}) for (float dy:{-1.9f,0.0f,1.9f}) {
        auto m=mob; m.x=x+dx; m.y=y+dy; m.z=z+6; m.health=5; m.maxHealth=10; m.hurtTime=10;
        std::vector<uint32_t> p(960*266+2,canary);
        renderMobOverlay(p.data()+1,960,266,world,0,0,{m},1);
        check(p.front()==canary&&p.back()==canary,"Offscreen health bar buffer canary overwrite");
    }
}
void originalWorldSmoke(const std::string& root)
{
    mcworld::World world;
    world.create(54321,root,true,864);
    check(world.playerX()>64&&world.playerZ()>64,"Original-world fixture must use absolute large-world coordinates");
    std::vector<Mob> mobs;
    for (int i=0;i<5;++i) mobs.push_back({i,MobKind(i),world.playerX()+(i-2)*1.5f,
        world.playerY()+8,world.playerZ()+8,155,0});
    const auto p=frame(world,mobs,960,266,0,43);
    check(Bounds(p,960,266).count>100,"Absolute original-world mobs must render outside legacy 64-cube range");
    Mob buried{1,MobKind::Cow,world.playerX(),1,world.playerZ()+4,180,0};
    check(Bounds(frame(world,{buried},960,266,0,-80),960,266).count==0,"Original world blockAt terrain must hide buried mobs");
    // Warm lazy original chunks before timing the actual main-view workload.
    std::vector<uint32_t> pixels(960*266,background);
    for (int i=0;i<5;++i) renderMobOverlay(pixels.data(),960,266,world,0,43,mobs,1);
    const auto start=std::chrono::steady_clock::now();
    for (int i=0;i<16;++i) renderMobOverlay(pixels.data(),960,266,world,0,43,mobs,1);
    std::cout<<"original-world absolute-coordinate smoke: 5 mobs render "
             <<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/16
             <<"ms/frame (warm), at player x="<<world.playerX()<<'\n';
}
void benchmark(const mcworld::World& world)
{
    std::vector<Mob> mobs;
    for (int i=0;i<5;++i) mobs.push_back({i,MobKind(i),world.playerX()+(i-2)*1.8f,
        world.playerY(),world.playerZ()+6+float(i%2),155,0});
    for (auto& mob:mobs) { mob.health=7; mob.maxHealth=20; mob.hurtTime=10; }
    volatile uint32_t sink=0;
    for (const auto& size:{std::array<int,2>{960,266},{1920,1080}}) {
        const int w=size[0],h=size[1]; std::vector<uint32_t> p(size_t(w)*h,background);
        for (int i=0;i<10;++i) renderMobOverlay(p.data(),w,h,world,0,0,mobs,1);
        std::vector<double> samples;
        for (int batch=0;batch<20;++batch) {
            const auto start=std::chrono::steady_clock::now();
            for (int i=0;i<8;++i) {
                for (auto& m:mobs) m.walkPhase=(batch*8+i)*0.08f;
                renderMobOverlay(p.data(),w,h,world,0,0,mobs,1);
            }
            samples.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/8);
            sink^=p[size_t(h/2)*w+w/2];
        }
        std::sort(samples.begin(),samples.end());
        std::cout<<"benchmark 5 mobs "<<w<<'x'<<h<<std::fixed<<std::setprecision(3)
                 <<" median="<<samples[10]<<"ms p95(batch-average)="<<samples[18]<<"ms\n";
        if (w==960) check(samples[10]<3,"Few-mob render exceeds 3ms median target at main 960x266 size");
        else if (samples[10]>=3) std::cout<<"NOTE: full-resolution 1080p exceeds main-view 3ms target on this host\n";
    }
    (void)sink;
}
} // namespace
int main(int argc,char** argv)
{
    try {
        hashes(argc>2?argv[2]:nullptr);
        TemporaryRoot root; mcworld::World world; world.create(12345,root.path,false);
        // Private in-memory test fixture, same as world-renderer-test.cpp. Clear
        // terrain without adding a mutable test API or modifying World itself.
        auto* blocks=const_cast<uint16_t*>(world.blockData());
        std::fill_n(blocks,size_t(mcworld::Width)*mcworld::Depth*mcworld::Height,uint16_t(0));
        models(world,argc>1?argv[1]:nullptr);
        occlusion(world,argc>1?argv[1]:nullptr);
        combat(world,argc>1?argv[1]:nullptr);
        guards(world);
        if (argc>3 && std::string(argv[3])=="--original") originalWorldSmoke(root.path);
        if (argc<4 || std::string(argv[3])!="--no-benchmark") benchmark(world);
        std::cout<<"PASS original PNG/RGBA hashes, five models, projection, animation, daylight, world/entity depth, health bars, hurt tint, near clip and buffer guards\n";
        return 0;
    } catch (const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; }
}
