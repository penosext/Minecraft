#include "ActorRenderer.hpp"
#include "SteveSkin.hpp"
#include "LegacyCatalog.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

// Standalone C++17 test, no image/system libraries. Optional first argument is
// an EXISTING output directory: writes PNGs directly with uncompressed DEFLATE.
// Compile without -DNDEBUG; checks below remain enabled with it as well.
namespace {
constexpr uint32_t kBackground = 0xff24364au;
constexpr uint32_t kCanary = 0xd35e79b1u;
constexpr float kPi = 3.14159265358979323846f;
void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
uint32_t rotr(uint32_t n, unsigned bits) { return (n >> bits) | (n << (32 - bits)); }
std::string sha256(std::vector<uint8_t> data)
{
    static constexpr uint32_t k[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    std::array<uint32_t, 8> state = {{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                                      0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19}};
    const uint64_t bits = uint64_t(data.size()) * 8;
    data.push_back(0x80);
    while (data.size() % 64 != 56) data.push_back(0);
    for (int i = 7; i >= 0; --i) data.push_back(uint8_t(bits >> (i * 8)));
    for (size_t offset = 0; offset < data.size(); offset += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = 0;
            for (int j = 0; j < 4; ++j) w[i] = (w[i] << 8) | data[offset + i * 4 + j];
        }
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = rotr(w[i-15],7) ^ rotr(w[i-15],18) ^ (w[i-15] >> 3);
            const uint32_t s1 = rotr(w[i-2],17) ^ rotr(w[i-2],19) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        auto v = state;
        for (int i = 0; i < 64; ++i) {
            const uint32_t s1 = rotr(v[4],6) ^ rotr(v[4],11) ^ rotr(v[4],25);
            const uint32_t ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
            const uint32_t t1 = v[7] + s1 + ch + k[i] + w[i];
            const uint32_t s0 = rotr(v[0],2) ^ rotr(v[0],13) ^ rotr(v[0],22);
            const uint32_t maj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
            const uint32_t t2 = s0 + maj;
            v = {{t1+t2,v[0],v[1],v[2],v[3]+t1,v[4],v[5],v[6]}};
        }
        for (int i = 0; i < 8; ++i) state[i] += v[i];
    }
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (uint32_t word : state)
        for (int shift = 28; shift >= 0; shift -= 4) result.push_back(digits[(word >> shift) & 15]);
    return result;
}
void be32(std::vector<uint8_t>& data, uint32_t value)
{
    for (int shift = 24; shift >= 0; shift -= 8) data.push_back(uint8_t(value >> shift));
}
void chunk(std::vector<uint8_t>& png, const char* type, const std::vector<uint8_t>& data)
{
    be32(png, uint32_t(data.size()));
    const size_t start = png.size();
    for (int i = 0; i < 4; ++i) png.push_back(uint8_t(type[i]));
    png.insert(png.end(), data.begin(), data.end());
    uint32_t crc = 0xffffffff;
    for (size_t i = start; i < png.size(); ++i) {
        crc ^= png[i];
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
    }
    be32(png, crc ^ 0xffffffffu);
}
void writePng(const std::string& path, const std::vector<uint32_t>& pixels, int w, int h)
{
    std::vector<uint8_t> raw;
    for (int y = 0; y < h; ++y) {
        raw.push_back(0); // PNG filter: none
        for (int x = 0; x < w; ++x) {
            const uint32_t p = pixels[size_t(y) * w + x];
            for (int shift : {16, 8, 0, 24}) raw.push_back(uint8_t(p >> shift));
        }
    }
    std::vector<uint8_t> zlib = {0x78, 0x01};
    for (size_t i = 0; i < raw.size();) {
        const size_t length = std::min(size_t(65535), raw.size() - i);
        zlib.push_back(i + length == raw.size() ? 1 : 0);
        zlib.push_back(uint8_t(length)); zlib.push_back(uint8_t(length >> 8));
        zlib.push_back(uint8_t(~length)); zlib.push_back(uint8_t((~length) >> 8));
        zlib.insert(zlib.end(), raw.begin() + i, raw.begin() + i + length);
        i += length;
    }
    uint32_t s1 = 1, s2 = 0;
    for (uint8_t b : raw) { s1 = (s1 + b) % 65521; s2 = (s2 + s1) % 65521; }
    be32(zlib, (s2 << 16) | s1);
    std::vector<uint8_t> png = {137,80,78,71,13,10,26,10};
    std::vector<uint8_t> ihdr;
    be32(ihdr, unsigned(w)); be32(ihdr, unsigned(h));
    ihdr.insert(ihdr.end(), {8,6,0,0,0});
    chunk(png, "IHDR", ihdr); chunk(png, "IDAT", zlib); chunk(png, "IEND", {});
    std::ofstream output(path, std::ios::binary);
    check(bool(output), "Cannot open output PNG (directory must already exist)");
    output.write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()));
    check(bool(output), "Cannot write output PNG");
}
std::vector<uint32_t> frame(int w, int h, int mode, float phase = 0, float swing = 0,
                            uint32_t background = kBackground)
{
    std::vector<uint32_t> pixels(size_t(w) * h, background);
    renderActorOverlay(pixels.data(), w, h, mode, phase, swing);
    return pixels;
}
struct Bounds {
    int x0, y0, x1 = -1, y1 = -1;
    size_t count = 0, sleeve = 0, skin = 0;
    double sleeveX = 0, sleeveY = 0, skinX = 0, skinY = 0;
    explicit Bounds(int w, int h) : x0(w), y0(h) {}
};
Bounds inspect(const std::vector<uint32_t>& pixels, int w, int h, uint32_t background)
{
    Bounds b(w, h);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        const uint32_t p = pixels[size_t(y) * w + x];
        if (p == background) continue;
        ++b.count;
        b.x0 = std::min(b.x0, x); b.x1 = std::max(b.x1, x);
        b.y0 = std::min(b.y0, y); b.y1 = std::max(b.y1, y);
        const int r = (p >> 16) & 255, g = (p >> 8) & 255, blue = p & 255;
        if (r < 20 && g > 50 && blue > 50) {
            ++b.sleeve; b.sleeveX += x; b.sleeveY += y;
        }
        if (r > 60 && r > g && g > blue && blue > 20) {
            ++b.skin; b.skinX += x; b.skinY += y;
        }
    }
    return b;
}
void testSkin(const char* sourcePath)
{
    check(steve::kSkinWidth == 64 && steve::kSkinHeight == 32, "Skin dimensions");
    check(std::string(steve::kSkinSourceSha256) ==
          "1a683a41813ff8b6f4633053b38412a506f2e6e666b0351941d0bb48f49534f0", "Source hash constant");
    check(sha256({'a','b','c'}) ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 self-test");
    std::vector<uint8_t> rgba;
    size_t transparent = 0, opaque = 0;
    for (uint32_t p : steve::kSkinPixels) {
        for (int shift : {16,8,0,24}) rgba.push_back(uint8_t(p >> shift));
        if ((p >> 24) == 0) ++transparent;
        if ((p >> 24) == 255) ++opaque;
    }
    check(transparent == 864 && opaque == 1184, "Source alpha counts");
    check(sha256(rgba) == steve::kSkinRgbaSha256, "Embedded RGBA SHA-256");
    check(sha256(rgba) == "5de568439f0433e51ed0757ab816d27d7a5a4dcfa746425fe8883c23f3276900",
          "Pinned decoded RGBA SHA-256");
    if (sourcePath) {
        std::ifstream input(sourcePath, std::ios::binary);
        check(bool(input), "Cannot read Steve PNG for file hash");
        std::vector<uint8_t> source((std::istreambuf_iterator<char>(input)), {});
        check(sha256(source) == steve::kSkinSourceSha256, "Actual source PNG SHA-256");
    }
}
void testEdges()
{
    renderActorOverlay(nullptr, 960, 266, 0, 0);
    uint32_t pixel = kCanary;
    for (const auto& size : {std::array<int,2>{0,1}, {1,0}, {-1,20}, {20,-1},
                             {16385,1}, {1,16385}, {std::numeric_limits<int>::max(),1}}) {
        renderActorOverlay(&pixel, size[0], size[1], 0, 0);
        check(pixel == kCanary, "Invalid size changed destination");
    }
    for (int mode : {-5,2,100}) {
        renderActorOverlay(&pixel, 1, 1, mode, 0);
        check(pixel == kCanary, "Invalid mode changed destination");
    }
    for (const auto& size : {std::array<int,2>{1,1}, {1,513}, {513,1}, {2,2},
                             {17,29}, {266,960}, {960,266}, {1920,1080}}) {
        const int w = size[0], h = size[1];
        for (int mode : {0,1}) for (float phase : {0.0f,1.57f,3.14f}) {
            std::vector<uint32_t> pixels(size_t(w) * h + 2, kCanary);
            renderActorOverlay(pixels.data()+1, w, h, mode, phase, 0.5f);
            check(pixels.front() == kCanary && pixels.back() == kCanary, "Buffer canary overwrite");
            for (size_t i = 1; i + 1 < pixels.size(); ++i)
                check(pixels[i] == kCanary || (pixels[i] >> 24) == 255, "Unexpected alpha on actor");
        }
    }
    for (int mode : {0,1}) {
        const auto idle = frame(960,266,mode);
        check(idle == frame(960,266,mode,std::numeric_limits<float>::quiet_NaN()), "NaN phase");
        check(idle == frame(960,266,mode,std::numeric_limits<float>::infinity()), "Infinite phase");
        check(idle == frame(960,266,mode,0,std::numeric_limits<float>::quiet_NaN()), "NaN swing");
        check(idle == frame(960,266,mode,0,-2), "Negative swing clamp");
        check(idle == frame(960,266,mode,0,2), "High swing clamp");
        check(idle == frame(960,266,mode,0,1), "Swing ends at idle");
        check(idle != frame(960,266,mode,0,0.5f), "Swing must change pose");
        check((idle != frame(960,266,mode,kPi/2)) == (mode==1),
              "Only preview maps legacy walkPhase; hand must not fabricate bob/swing");
        check(idle == frame(960,266,mode,2*kPi), "Phase wrap");
        const auto transparent = frame(960,266,mode,0,0,0x00112233u);
        const auto partial = frame(960,266,mode,0,0,0x7f223344u);
        for (size_t i = 0; i < idle.size(); ++i) {
            if (idle[i] == kBackground) {
                check(transparent[i] == 0x00112233u && partial[i] == 0x7f223344u,
                      "Outside silhouette changed alpha or RGB");
            } else {
                check(transparent[i] == idle[i] && partial[i] == idle[i], "Opaque skin compositing");
            }
        }
        const Bounds b = inspect(idle,960,266,kBackground);
        std::cout << "diagnostic mode=" << mode << " bbox=" << b.x0 << ',' << b.y0 << ','
                  << b.x1 << ',' << b.y1 << " count=" << b.count << " sleeve=" << b.sleeve
                  << " skin=" << b.skin << '\n';
        check(b.count > 500 && b.count < 960*266/10, "Overlay coverage too small/large");
        check(b.skin > 50 && (mode==0 || b.sleeve>50), "Expected original visible skin texels");
        check(b.y0 > 266/4, "Overlay obscures upper scene");
        if (mode == 0) {
            check(b.x0 >= 576 && b.x0 <= 578 && b.y0 >= 187 && b.y0 <= 188 &&
                  b.x1==670 && b.y1==265,
                  "09df idle 960x266 projected/clipped raster bbox");
            check(b.y1 >= 264, "Sleeve must connect from bottom edge");
            check(b.sleeve==0, "Original idle 960x266 clips the sleeve below the viewport");
            // Attack trajectory is tested against original ordered transforms,
            // not an invented requirement to move upper-left at progress .5.
            // A single convex cuboid should have no transparent seams/atlas holes.
            for (int y = b.y0; y <= b.y1; ++y) {
                int first = 960, last = -1;
                for (int x = 0; x < 960; ++x)
                    if (idle[size_t(y)*960+x] != kBackground) { first = std::min(first,x); last=x; }
                for (int x = first; x <= last; ++x)
                    check(idle[size_t(y)*960+x] != kBackground, "Arm has a transparent internal seam");
            }
        } else {
            check(b.x0 > 960*0.35 && b.x1 < 960*0.65, "Preview must stay centered");
            check(b.y1 < 266*0.91, "Preview should leave bottom UI clearance");
            std::set<uint32_t> colors(idle.begin(),idle.end());
            check(colors.size() > 35, "Must sample a skin, not paint flat-colored boxes");
        }
        std::cout << "mode=" << mode << " idle bbox=[" << b.x0 << ',' << b.y0 << ".."
                  << b.x1 << ',' << b.y1 << "] pixels=" << b.count
                  << " sleeve=" << b.sleeve << " skin=" << b.skin << '\n';
    }
    // Across the full walk/action range, validate UI margins at target landscape size.
    for (int mode : {0,1}) for (int step = 0; step < 32; ++step) {
        const auto pixels = frame(960,266,mode,2*kPi*step/32, float(step%9)/8);
        const Bounds b = inspect(pixels,960,266,kBackground);
        check(b.x0>=0 && b.y0>=0 && b.x1<960 && b.y1<266 && b.count < 960*266/4,
              "Original animation remains clipped to viewport");
        if (mode == 1) check(b.y1 < 266*0.91, "Animated preview bottom clearance");
    }
}
// Independent double-precision per-point reference. This deliberately does not
// use renderer matrices or generated constants; each call below is the inverse
// traversal of the verified gl-call list (postmultiply acts right-to-left).
struct ReferencePoint {
    double x,y,z;
    void move(double a,double b,double c) {x+=a;y+=b;z+=c;}
    void rotate(double degrees,int axis) {
        const double a=degrees*3.14159265358979323846/180,c=std::cos(a),s=std::sin(a);
        const double u=x,v=y,t=z;
        if(axis==0) {y=c*v-s*t;z=s*v+c*t;}
        if(axis==1) {x=c*u+s*t;z=-s*u+c*t;}
        if(axis==2) {x=c*u-s*v;y=s*u+c*v;}
    }
};
float referenceSin(float value,bool cosine=false) {
    const float pi=3.141592654f;
    const int index=int(value*(65536.0f/(pi*2))+(cosine?16384:0))&65535;
    return float(std::sin(float(index)*pi*2/65536.0f));
}
ReferencePoint referenceHand(FirstPersonVertex p,const FirstPersonState& state) {
    constexpr double pi=3.14159265358979323846;
    ReferencePoint r{p.x/16.0,p.y/16.0,p.z/16.0};
    r.rotate(.1*180/pi,2);r.move(-5.0/16,2.0/16,0);
    r.move(5.6,0,0);r.rotate(-135,1);r.rotate(200,0);r.rotate(120,2);r.move(-1,3.6,3.5);
    const float swing=state.swing;
    const double s1=referenceSin(swing*kPi),s2=referenceSin(float(std::sqrt(swing))*kPi);
    r.rotate(-referenceSin(swing*swing*kPi)*20,2);r.rotate(s2*70,1);r.rotate(45,1);
    r.move(state.viewport==FirstPersonViewport::SplitVertical?-.18:0,
           state.viewport==FirstPersonViewport::SplitHorizontal?.08:0,0);
    r.move(.8*.8,-.75*.8-(1-state.equip)*.6,-.9*.8);
    r.move(-s2*.3,referenceSin(float(std::sqrt(swing))*kPi*2)*.4,-s1*.4);
    r.rotate(state.yawLag*.1,1);r.rotate(state.pitchLag*.1,0);
    if(state.bob) {
        const float b=state.bobPhase,amount=state.bobAmount;
        r.rotate(state.bobTilt,0);r.rotate(std::abs(referenceSin(b*kPi-.2f,true)*amount)*5,0);
        r.rotate(referenceSin(b*kPi)*amount*3,2);
        r.move(referenceSin(b*kPi)*amount*.5,-std::abs(referenceSin(b*kPi,true)*amount),0);
    }
    return r;
}
void close(double actual,double expected,double tolerance,const char* message) {
    if(!std::isfinite(actual) || std::abs(actual-expected)>tolerance) {
        std::cerr << message << " actual=" << actual << " expected=" << expected << '\n';
        check(false,message);
    }
}
std::vector<uint32_t> originalFrame(int w,int h,const FirstPersonState& state,
                                   FirstPersonDiagnostics* diagnostics=nullptr,uint32_t bg=kBackground) {
    std::vector<uint32_t> pixels(size_t(w)*h,bg);
    renderOriginalHand(pixels.data(),w,h,state,diagnostics);
    return pixels;
}
void testOriginalHand() {
    FirstPersonState state;
    FirstPersonDiagnostics idle;
    const auto pixels=originalFrame(960,266,state,&idle);
    check(idle.valid && idle.triangleCount>0,"Original diagnostics and triangles");
    check(pixels==frame(960,266,0),"Old API must use original hand state");
    check(idle.rasterBounds==std::array<int,4>{{576,187,672,266}},"Conservative idle raster bounds");
    close(idle.aspect,960.0/266,1e-6,"Actual controller viewport aspect");
    close(idle.fov,70,0,"Original default hand FOV");
    const double pinned[8][3]={{.41177984,-.69671165,-.45578112},{.32857908,-.86739291,-.61840197},
        {.60507689,-.46186314,-1.18549505},{.68827765,-.29118188,-1.02287420},
        {.62876619,-.81957416,-.43784431},{.54556542,-.99025542,-.60046516},
        {.82206323,-.58472565,-1.16755824},{.90526399,-.41404439,-1.00493739}};
    for(int i=0;i<8;++i) {
        close(idle.cameraVertices[i].x,pinned[i][0],3e-6,"Pinned idle camera X");
        close(idle.cameraVertices[i].y,pinned[i][1],3e-6,"Pinned idle camera Y");
        close(idle.cameraVertices[i].z,pinned[i][2],3e-6,"Pinned idle camera Z");
    }
    // The fist (local +Y) is above and left of its sleeve; no UV flips are used
    // to fake that orientation. The sleeve is genuinely outside this viewport.
    check(idle.projectedVertices[3].x<idle.projectedVertices[0].x &&
          idle.projectedVertices[3].y<idle.projectedVertices[0].y &&
          idle.projectedVertices[0].y>266,"Fist/sleeve geometric orientation");
    close(idle.modelVertices[0].x,-3,0,"Right arm original origin");
    close(idle.modelVertices[6].x-idle.modelVertices[0].x,4,0,"Original width 4");
    close(idle.modelVertices[6].y-idle.modelVertices[0].y,12,0,"Original height 12");
    close(idle.modelVertices[6].z-idle.modelVertices[0].z,4,0,"Original depth 4");
    const float rectangles[6][4]={{48,20,52,32},{40,20,44,32},{44,16,48,20},
                                {48,20,52,16},{44,20,48,32},{52,20,56,32}};
    for(int f=0;f<6;++f) {
        const auto& t=rectangles[f];const float us=.1f,vs=t[3]>t[1]?.1f:-.1f;
        const float expected[4][2]={{t[2]-us,t[1]+vs},{t[0]+us,t[1]+vs},
                                   {t[0]+us,t[3]-vs},{t[2]-us,t[3]-vs}};
        for(int i=0;i<4;++i) {
            close(idle.faceUV[f][i].u,expected[i][0],1e-5,"Polygon left/right signed inset");
            close(idle.faceUV[f][i].v,expected[i][1],1e-5,"Polygon up/down signed inset");
        }
    }
    for(auto viewport:{FirstPersonViewport::FullViewport,FirstPersonViewport::SplitHorizontal,
                       FirstPersonViewport::SplitVertical})
        for(float swing:{0.0f,.13f,.5f,.91f,1.0f}) for(float equip:{0.0f,.35f,1.0f}) {
            FirstPersonState s;s.swing=swing;s.equip=equip;s.viewport=viewport;
            s.bob=true;s.bobPhase=.36f;s.bobAmount=.07f;s.bobTilt=1.25f;
            s.pitchLag=3.5f;s.yawLag=-2.25f;
            FirstPersonDiagnostics d;renderOriginalHand(nullptr,960,266,s,&d);
            for(int i=0;i<8;++i) {
                const auto r=referenceHand(d.modelVertices[i],s);
                if(std::abs(d.cameraVertices[i].x-r.x)>8e-6) std::cerr << "case viewport=" << int(viewport)
                    << " swing=" << swing << " equip=" << equip << " vertex=" << i << '\n';
                close(d.cameraVertices[i].x,r.x,8e-6,"Ordered original transform X");
                close(d.cameraVertices[i].y,r.y,8e-6,"Ordered original transform Y");
                close(d.cameraVertices[i].z,r.z,8e-6,"Ordered original transform Z");
                if(r.z<-.05) {
                    const double cot=1/std::tan(d.fov*3.14159265358979323846/360);
                    close(d.projectedVertices[i].x,(1+cot/d.aspect*r.x/-r.z)*480,.01,"Same viewport camera X");
                    close(d.projectedVertices[i].y,(1-cot*r.y/-r.z)*133,.01,"Same viewport camera Y");
                }
            }
        }
    for(const auto size:{std::array<int,2>{960,266},{1920,532},{266,960},{1,513},{513,1}}) {
        FirstPersonDiagnostics d;renderOriginalHand(nullptr,size[0],size[1],{},&d);
        const double cot=1/std::tan(70*3.14159265358979323846/360);
        close(d.projection[0],cot*size[1]/size[0],1e-4,"Viewport projection X scale");
        close(d.projection[5],cot,1e-6,"Viewport projection Y scale");
        for(int i=0;i<8;++i) {
            close(d.modelView[i],idle.modelView[i],0,"Same model transform for every viewport");
            const auto& c=d.cameraVertices[i];
            close(d.projectedVertices[i].x,size[0]*.5+size[1]*.5*cot*c.x/-c.z,.002,
                  "Same viewport camera dimensions, not a hardcoded 16:9");
        }
        if(size[0]==1920 && size[1]==532) for(int i=0;i<8;++i) {
            close(d.projectedVertices[i].x,idle.projectedVertices[i].x*2,.002,"Double viewport X");
            close(d.projectedVertices[i].y,idle.projectedVertices[i].y*2,.002,"Double viewport Y");
        }
    }
    // Explicit split aspect means parent framebuffer ratio. Derived aspect is
    // the child ratio, so callers must not pass child aspect then multiply twice.
    FirstPersonDiagnostics split;
    state.viewport=FirstPersonViewport::SplitHorizontal;state.aspect=16.0f/9;
    renderOriginalHand(nullptr,960,270,state,&split);
    close(split.aspect,32.0/9,1e-6,"Original horizontal parent aspect multiplier");
    close(split.fov,49,1e-5,"Original horizontal FOV .7");
    state.viewport=FirstPersonViewport::SplitVertical;
    renderOriginalHand(nullptr,480,540,state,&split);
    close(split.aspect,8.0/9,1e-6,"Original vertical parent aspect multiplier");
    close(split.fov,70,0,"Vertical split keeps hand FOV");
    state={};state.fov=60;FirstPersonDiagnostics water;
    renderOriginalHand(nullptr,960,266,state,&water);
    check(water.modelView==idle.modelView && water.projection!=idle.projection,"FOV changes only projection");
    close(water.fov,60,0,"Caller-resolved water FOV");
    // Guard and NaN/Inf tests for every public numeric state input.
    for(float value:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
        state={};state.aspect=value;state.fov=value;state.swing=value;state.equip=value;
        state.bob=true;state.bobPhase=value;state.bobAmount=value;state.bobTilt=value;
        state.pitchLag=value;state.yawLag=value;state.farPlane=value;
        check(originalFrame(960,266,state)==pixels,"Nonfinite state becomes idle");
    }
    renderOriginalHand(nullptr,960,266,{},nullptr);
    FirstPersonDiagnostics invalid;renderOriginalHand(nullptr,0,266,{},&invalid);
    check(!invalid.valid,"Invalid diagnostic size");
    for(int w:{1,17,960}) for(int h:{1,29,266}) for(float fov:{1.0f,179.0f}) {
        state={};state.fov=fov;state.aspect=std::numeric_limits<float>::max();
        state.bob=true;state.bobAmount=1;state.bobPhase=.5f;state.swing=.5f;
        std::vector<uint32_t> p(size_t(w)*h+2,kCanary);
        renderOriginalHand(p.data()+1,w,h,state,&invalid);
        check(p.front()==kCanary && p.back()==kCanary,"State clipping guard");
    }
    // Main API inventory catalog indices. Stone and grass are supported; a
    // special shape/invalid index is not silently drawn as a guessed cube/hand.
    for(int index:{0,2,3}) {
        state={};state.heldBlockIndex=index;
        FirstPersonDiagnostics d;const auto held=originalFrame(960,266,state,&d);
        check(d.heldBlockSupported && held!=pixels && d.triangleCount>0,"Explicit held cube");
        std::vector<uint32_t> via(pixels.size(),kBackground);
        renderActorOverlay(via.data(),960,266,0,123,0,index);
        check(via==held,"HeldBlockIndex API forwarding");
        for(const auto& vertex:d.modelVertices) check(vertex.x>=0&&vertex.x<=1&&vertex.y>=0&&vertex.y<=1,
                                                    "Held model cell-local, centered by source matrix");
    }
    int glass=-1;
    for(int i=0;i<legacy::kBlockCount;++i) if(std::string(legacy::kBlockSpecs[i].key)=="glass") glass=i;
    check(glass>=0,"Glass catalog fixture");
    state={};state.heldBlockIndex=glass;
    const auto glassFrame=originalFrame(960,266,state,&invalid,0x00112233u);
    check(invalid.heldBlockSupported,"Glass cube supported for alpha-zero tests");
    const auto box=invalid.rasterBounds;
    size_t holes=0,opaque=0;
    for(int y=box[1];y<box[3];++y) for(int x=box[0];x<box[2];++x) {
        const uint32_t p=glassFrame[size_t(y)*960+x];
        if(p==0x00112233u) ++holes;
        else {++opaque;check(p>>24,"Alpha-zero texel overwrote destination RGB");}
    }
    check(holes>100 && opaque>100,"Transparent source texels preserve destination color/depth");
    state={};state.heldBlockIndex=std::numeric_limits<int>::max();
    const auto unsupported=originalFrame(960,266,state,&invalid);
    check(!invalid.heldBlockSupported && std::all_of(unsupported.begin(),unsupported.end(),
          [](uint32_t p){return p==kBackground;}),"Unsupported selection must not guess");
    std::cout << "PASS original matrix/vertices, idle bbox, Polygon UV orientation, viewport/FOV/split, state guards, held cube\n";
}
void benchmark()
{
    volatile uint32_t sink = 0;
    for (const auto& size : {std::array<int,2>{960,266}, {1920,1080}}) for (int mode : {0,1}) {
        const int w=size[0], h=size[1];
        std::vector<uint32_t> pixels(size_t(w)*h,kBackground);
        const auto coldStart = std::chrono::steady_clock::now();
        renderActorOverlay(pixels.data(),w,h,mode,0);
        const double cold = std::chrono::duration<double,std::milli>(
            std::chrono::steady_clock::now()-coldStart).count();
        for (int i=0;i<40;++i) renderActorOverlay(pixels.data(),w,h,mode,i*0.08f,0.4f);
        std::vector<double> samples;
        for (int batch=0;batch<30;++batch) {
            const auto start=std::chrono::steady_clock::now();
            for (int i=0;i<16;++i) renderActorOverlay(pixels.data(),w,h,mode,(batch*16+i)*0.08f,0.4f);
            samples.push_back(std::chrono::duration<double,std::milli>(
                std::chrono::steady_clock::now()-start).count()/16);
            sink ^= pixels[size_t(h/2)*w+w/2];
        }
        std::sort(samples.begin(),samples.end());
        std::cout << "benchmark " << w << 'x' << h << " mode=" << mode << std::fixed
                  << std::setprecision(3) << " first=" << cold << "ms median=" << samples[15]
                  << "ms p95(batch-average)=" << samples[28] << "ms\n";
        if (samples[15] >= 2.0) std::cout << "NOTE: median exceeds 2ms target on this host\n";
    }
    (void)sink;
}
} // namespace

int main(int argc, char** argv)
{
    try {
        testSkin(argc > 2 ? argv[2] : nullptr);
        testOriginalHand();
        testEdges();
        if (argc > 1) {
            const std::string dir=argv[1];
            for (int mode : {0,1}) for (int pose=0;pose<3;++pose) {
                const auto pixels=frame(960,266,mode,pose==1 ? kPi/2 : 0,pose==2 ? 0.5f : 0);
                const std::string name=(mode==0 ? "arm-" : "steve-preview-") +
                                       std::string(pose==0 ? "idle" : pose==1 ? "walk" : "swing");
                writePng(dir+"/"+name+".png",pixels,960,266);
            }
            writePng(dir+"/steve-preview-alpha.png",frame(960,266,1,0,0,0),960,266);
            writePng(dir+"/arm-alpha.png",frame(960,266,0,0,0,0),960,266);
            writePng(dir+"/steve-preview-portrait.png",frame(266,960,1),266,960);
            writePng(dir+"/one-pixel.png",frame(1,1,0),1,1);
        }
        benchmark();
        std::cout << "PASS skin hashes, 1x1/invalid/guard/alpha, texture coverage, animation, PNG output\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
