#include "WorldRenderer.hpp"
#include "World.hpp"
#include "LegacyBlock.hpp"
#include "LegacyCatalog.hpp"
#include "LegacyTextures.hpp"
#include "SoftwareRenderer.hpp"
#include "../../../port/assets/sky/SkyTextures.hpp"
#include "../../../port/assets/sky/LightingTables.hpp"
#include <limits>
#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>
#include <vector>

namespace worldrender {
float verticalFov() { return 70.0f; }
float focalLength(int, int height) { return height / (2 * std::tan(verticalFov() * 0.008726646259971648f)); }
TileLighting tileLighting(uint16_t catalogId)
{
    if (!catalogId || catalogId > legacy::kBlockCount) return {0,0,false};
    const int id = legacy::kBlockSpecs[catalogId-1].tileId;
    if (unsigned(id) >= 256) return {0,0,false};
    const auto& tile = sourcelighting::kTiles[id];
    return {tile.opacity,tile.emission,tile.ao};
}
namespace {
const std::vector<TileLighting>& catalogLighting()
{
    static const auto result=[] {
        std::vector<TileLighting> table(65536, {0,0,false});
        for (int id=1;id<=legacy::kBlockCount;++id) table[id]=tileLighting(uint16_t(id));
        return table;
    }();
    return result;
}
thread_local LightingBuffer renderLighting;
}
const LightingBuffer& lightingBuffer() { return renderLighting; }

// [FIX 1] sample(): out-of-window and unloaded columns are "unknown", not
// "dark air". Returning {0,0} made the AO neighbourhood inconsistent with
// occlusion(), producing the bright/dark flicker at the resident border.
// Return the outdoor assumption {15,0} so sampling and AO agree.
LightSample LightingBuffer::sample(int x, int y, int z) const
{
    if (unsigned(x-originX)>=unsigned(width) || unsigned(z-originZ)>=unsigned(depth) || y<originY)
        return {15,0};
    if (loadedColumns && !loadedColumns[size_t(z-originZ)*width+x-originX]) return {15,0};
    if (y>=originY+height) return {15,0};
    const size_t i=(size_t(y-originY)*depth+z-originZ)*width+x-originX;
    return {sky[i],block[i]};
}

bool LightingBuffer::refreshAround(const mcworld::RenderVolume& v, int wx, int wy, int wz, int radius)
{
    // Only a cache that already matches this exact window can be patched; a
    // geometry change still needs the normal rebuild path.
    if (!v.data || source != v.data || originX != v.originX || originY != v.originY || originZ != v.originZ ||
        width != v.width || height != v.height || depth != v.depth) return false;
    if (sky.size() != size_t(width) * height * depth || block.size() != sky.size()) return false;
    if (radius <= 0 || radius > 17) return false;
    const int x = wx - originX, y = wy - originY, z = wz - originZ;
    if (unsigned(x) >= unsigned(width) || unsigned(y) >= unsigned(height) || unsigned(z) >= unsigned(depth))
        return false;
    const int layer = width * depth;
    const int x0 = std::max(0, x - radius), x1 = std::min(width - 1, x + radius);
    const int z0 = std::max(0, z - radius), z1 = std::min(depth - 1, z + radius);
    const auto* metadata = catalogLighting().data();
    const auto unloaded = [&](int i) {
        if (!v.loadedColumns) return false;
        return !v.loadedColumns[size_t((i / width) % depth) * width + i % width];
    };
    const auto loss = [&](int i) {
        if (unloaded(i)) return 16;
        const auto& t = metadata[source[i]];
        return t.lightBlock >= 15 && t.emission ? 1 : std::max(1, int(t.lightBlock));
    };
    // Reseed the affected columns top-down, exactly like a full rebuild, then
    // re-propagate from the box border inwards so a newly opened shaft lights up
    // immediately. Light arriving from outside the box is taken from the border.
    for (int cz = z0; cz <= z1; ++cz) for (int cx = x0; cx <= x1; ++cx) {
        if (v.loadedColumns && !v.loadedColumns[size_t(cz) * width + cx]) {
            for (int cy = height - 1; cy >= 0; --cy) {
                const int i = (cy * depth + cz) * width + cx; sky[i] = 0; block[i] = 0;
            }
            continue;
        }
        int level = 15; bool direct = true;
        for (int cy = height - 1; cy >= 0; --cy) {
            const int i = (cy * depth + cz) * width + cx;
            const int opacity = metadata[source[i]].lightBlock;
            if (opacity) direct = false;
            if (!direct) level = std::max(0, level - std::max(1, opacity));
            sky[i] = uint8_t(level);
            block[i] = metadata[source[i]].emission;
        }
    }
    std::vector<int> queue;
    const auto neighbours = [&](int i, const auto& visit) {
        const int cx = i % width, cz = (i / width) % depth, cy = i / layer;
        const auto inside = [&](int n) {
            const int nx = n % width, nz = (n / width) % depth;
            if (nx >= x0 && nx <= x1 && nz >= z0 && nz <= z1 && !unloaded(n)) visit(n);
        };
        if (cx > 0) inside(i - 1);
        if (cx + 1 < width) inside(i + 1);
        if (cz > 0) inside(i - width);
        if (cz + 1 < depth) inside(i + width);
        if (cy > 0) inside(i - layer);
        if (cy + 1 < height) inside(i + layer);
    };
    const auto flood = [&](std::vector<uint8_t>& values) {
        for (size_t front = 0; front < queue.size(); ++front) {
            const int i = queue[front];
            const int light = values[i];
            if (light <= 1) continue;
            neighbours(i, [&](int n) {
                const int candidate = light - loss(n);
                if (candidate > values[n]) { values[n] = uint8_t(candidate); queue.push_back(n); }
            });
        }
        queue.clear();
    };
    // Seed from one cell outside the reseeded box too, so existing outside light
    // flows back in; the box itself seeds its own emitters and sky columns.
    const int sx0 = std::max(0, x0 - 1), sx1 = std::min(width - 1, x1 + 1);
    const int sz0 = std::max(0, z0 - 1), sz1 = std::min(depth - 1, z1 + 1);
    for (int cz = sz0; cz <= sz1; ++cz) for (int cx = sx0; cx <= sx1; ++cx)
        for (int cy = 0; cy < height; ++cy) {
            const int i = (cy * depth + cz) * width + cx;
            if (unloaded(i) || sky[i] <= 1) continue;
            bool needed = false;
            neighbours(i, [&](int n) { if (int(sky[i]) - loss(n) > sky[n]) needed = true; });
            if (needed) queue.push_back(i);
        }
    flood(sky);
    for (int cz = sz0; cz <= sz1; ++cz) for (int cx = sx0; cx <= sx1; ++cx)
        for (int cy = 0; cy < height; ++cy) {
            const int i = (cy * depth + cz) * width + cx;
            if (!unloaded(i) && block[i] > 1) queue.push_back(i);
        }
    flood(block);
    ++localRefreshes;
    return true;
}

bool LightingBuffer::update(const mcworld::RenderVolume& v, std::string_view cachePath, uint64_t terrainRevision,
                            int columnBudget)
{
    if (!v.data || v.width<=0 || v.height<=0 || v.depth<=0) return false;
    // [FIX 2a] A window rebuild swaps the loadedColumns vector; the pointer
    // then refers to freed/reused storage. Treat a pointer change as a
    // geometry change so the incremental path cannot seed/spread/sample
    // against stale markers.
    const bool sameGeometry = path==cachePath && originX==v.originX && originY==v.originY &&
        originZ==v.originZ && width==v.width && height==v.height && depth==v.depth &&
        loadedColumns==v.loadedColumns;
    if(source==v.data && sameGeometry && revision==terrainRevision && pendingColumn<0) return false;
    const uint64_t count=uint64_t(v.width)*v.height*v.depth;
    if(count>uint64_t(std::numeric_limits<int>::max())) return false;
    // Only a pure revision change on an already-built identical window may be
    // spread over frames. Geometry changes and the first build must finish now,
    // otherwise sampling would read a stale origin or uninitialised cells.
    const bool canDefer = columnBudget>0 && sameGeometry && !sky.empty() &&
        sky.size()==size_t(count) && block.size()==size_t(count);
    if (!canDefer) { pendingColumn=-1; pendingSky.clear(); pendingBlock.clear(); pendingSource=nullptr; }
    const auto* metadata=catalogLighting().data();
    const int layer=v.width*v.depth;
    // [FIX 2b] Commit the new marker pointer BEFORE any seeding/scanning below
    // so the same update() sees a consistent view of "which columns are
    // resident". (Previously this assignment happened after canDefer.)
    loadedColumns = v.loadedColumns;

    if (canDefer) {
        const int columns = v.width*v.depth;
        // Restart the rebuild if terrain advanced again mid-flight.
        if (pendingColumn<0 || pendingSource!=v.data || pendingRevision!=terrainRevision) {
            pendingSky.assign(size_t(count),0); pendingBlock.assign(size_t(count),0);
            pendingSource=v.data; pendingRevision=terrainRevision; pendingColumn=0;
            pendingStage=Stage::Seed; pendingQueue.clear(); pendingFront=0; pendingScan=0;
            ++incrementalBuilds;
            // Rebuild what the player is looking at first: order columns by the
            // dot product with the view direction, then by distance to the eye.
            pendingOrder.resize(size_t(columns));
            for(int c=0;c<columns;++c) pendingOrder[size_t(c)]=c;
            if(viewValid) {
                const float ex=viewEyeX-v.originX, ez=viewEyeZ-v.originZ;
                const auto score=[&](int c) {
                    const float dx=float(c%v.width)+.5f-ex, dz=float(c/v.width)+.5f-ez;
                    const float len=std::sqrt(dx*dx+dz*dz);
                    // Ahead of the camera sorts before behind it; ties by distance.
                    const float facing=len>1e-3f?(dx*viewDirX+dz*viewDirZ)/len:1.0f;
                    return len-facing*float(v.width+v.depth);
                };
                std::stable_sort(pendingOrder.begin(),pendingOrder.end(),
                                 [&](int a,int b){ return score(a)<score(b); });
            }
        }
        ++incrementalSteps;
        const uint16_t* seedSource = v.data;
        const int cells = int(count);
        // Every stage below shares one work budget, so no single call can run
        // long regardless of which stage it happens to be in.
        int work = int(std::min<int64_t>(int64_t(columnBudget) * v.height, count));
        const auto stepNeighbours=[&](int i, const auto& visit) {
            const int x=i%v.width,z=(i/v.width)%v.depth,y=i/layer;
            if(x>0)visit(i-1);
            if(x+1<v.width)visit(i+1);
            if(z>0)visit(i-v.width);
            if(z+1<v.depth)visit(i+v.width);
            if(y>0)visit(i-layer);
            if(y+1<v.height)visit(i+layer);
        };
        // Unloaded columns absorb light completely, so a loaded neighbour cannot
        // bleed brightness into terrain that has not arrived yet.
        const auto unloaded=[&](int n) {
            if(!v.loadedColumns) return false;
            return !v.loadedColumns[size_t((n/v.width)%v.depth)*v.width+n%v.width];
        };
        const auto loss=[&](int n) {
            if(unloaded(n)) return 16;
            const auto& t=metadata[seedSource[n]];
            return t.lightBlock>=15&&t.emission?1:std::max(1,int(t.lightBlock));
        };
        const auto spreadStep=[&](std::vector<uint8_t>& values) {
            while(pendingFront<pendingQueue.size() && work>0) {
                const int i=pendingQueue[pendingFront++];
                --work;
                const int light=values[i];
                if(light<=1)continue;
                stepNeighbours(i,[&](int n) {
                    const int candidate=light-loss(n);
                    if(candidate>values[n]){values[n]=uint8_t(candidate);pendingQueue.push_back(n);}
                });
            }
            if(pendingFront<pendingQueue.size()) return false;
            pendingQueue.clear(); pendingFront=0; pendingScan=0; return true;
        };
        while (work>0) {
            if (pendingStage==Stage::Seed) {
                const int end=std::min(columns,pendingColumn+std::max(1,work/std::max(1,v.height)));
                for(int c=pendingColumn;c<end;++c) {
                    // Visit order is view-first when an order was supplied.
                    const int col=pendingOrder.empty()?c:pendingOrder[size_t(c)];
                    const int x=col%v.width, z=col/v.width;
                    // A column whose chunk is not resident is padding, not air:
                    // leave it fully dark so skylight cannot pour in sideways.
                    if(v.loadedColumns && !v.loadedColumns[size_t(z)*v.width+x]) {
                        for(int y=v.height-1;y>=0;--y) {
                            const int i=(y*v.depth+z)*v.width+x;
                            pendingSky[i]=0; pendingBlock[i]=0;
                        }
                        continue;
                    }
                    int level=15;bool direct=true;
                    for(int y=v.height-1;y>=0;--y) {
                        const int i=(y*v.depth+z)*v.width+x;
                        const int opacity=metadata[v.data[i]].lightBlock;
                        if(opacity)direct=false;
                        if(!direct)level=std::max(0,level-std::max(1,opacity));
                        pendingSky[i]=uint8_t(level);
                        pendingBlock[i]=metadata[v.data[i]].emission;
                    }
                }
                work-=(end-pendingColumn)*v.height;
                pendingColumn=end;
                if(pendingColumn>=columns){pendingStage=Stage::SeedSky;pendingScan=0;}
                continue;
            }
            if (pendingStage==Stage::SeedSky) {
                int i=pendingScan;
                for(;i<cells && work>0;++i) {
                    --work;
                    if(pendingSky[i]<=1)continue;
                    bool needed=false;
                    stepNeighbours(i,[&](int n){ if(int(pendingSky[i])-loss(n)>pendingSky[n])needed=true; });
                    if(needed)pendingQueue.push_back(i);
                }
                pendingScan=i;
                if(pendingScan>=cells){pendingStage=Stage::SpreadSky;pendingFront=0;}
                continue;
            }
            if (pendingStage==Stage::SpreadSky) {
                if(!spreadStep(pendingSky))continue;
                pendingStage=Stage::SeedBlock;pendingScan=0;continue;
            }
            if (pendingStage==Stage::SeedBlock) {
                int i=pendingScan;
                for(;i<cells && work>0;++i){--work;if(pendingBlock[i]>1)pendingQueue.push_back(i);}
                pendingScan=i;
                if(pendingScan>=cells){pendingStage=Stage::SpreadBlock;pendingFront=0;}
                continue;
            }
            if(!spreadStep(pendingBlock))continue;
            // Publish only now: sampling never observes a partially lit volume.
            sky.swap(pendingSky); block.swap(pendingBlock);
            pendingSky.clear(); pendingBlock.clear();
            // Retain scratch capacity for the next revision instead of reallocating each commit.
            pendingColumn=-1; pendingSource=nullptr; pendingStage=Stage::Seed;
            source=v.data; revision=terrainRevision; ++builds;
            return true;
        }
        return false; // keep rendering the last completed result
    }

    source=v.data;path=cachePath;revision=terrainRevision;
    originX=v.originX;originY=v.originY;originZ=v.originZ;width=v.width;height=v.height;depth=v.depth;
    sky.assign(size_t(count),0);block.assign(size_t(count),0);
    // Seed direct columns; after the first blocker even air attenuates by one,
    // matching LevelChunk::recalcHeight's below-heightmap update.
    for(int z=0;z<depth;++z) for(int x=0;x<width;++x) {
        // Columns without a resident chunk are padding, not real air: keep them
        // dark so skylight cannot leak through unloaded terrain.
        if(v.loadedColumns && !v.loadedColumns[size_t(z)*width+x]) {
            for(int y=height-1;y>=0;--y) { const int i=(y*depth+z)*width+x; sky[i]=0; block[i]=0; }
            continue;
        }
        int level=15;bool direct=true;
        for(int y=height-1;y>=0;--y) {
            const int i=(y*depth+z)*width+x;
            const int opacity=metadata[source[i]].lightBlock;
            if(opacity)direct=false;
            if(!direct)level=std::max(0,level-std::max(1,opacity));
            sky[i]=uint8_t(level);
            block[i]=metadata[source[i]].emission;
        }
    }
    // Only seed sky cells that can improve a neighbour: an open sky must not
    // allocate a queue entry for every cell on every cache rebuild.
    std::vector<int> queue;
    const auto neighbours=[&](int i, const auto& visit) {
        const int x=i%width,z=(i/width)%depth,y=i/layer;
        if(x>0)visit(i-1);
        if(x+1<width)visit(i+1);
        if(z>0)visit(i-width);
        if(z+1<depth)visit(i+width);
        if(y>0)visit(i-layer);
        if(y+1<height)visit(i+layer);
    };
    // Unloaded columns absorb light completely, matching the seeding above.
    const auto columnUnloaded=[&](int n) {
        if(!v.loadedColumns) return false;
        return !v.loadedColumns[size_t((n/width)%depth)*width+n%width];
    };
    const auto lossAt=[&](int n) {
        if(columnUnloaded(n)) return 16;
        const auto& t=metadata[source[n]];
        return t.lightBlock>=15&&t.emission?1:std::max(1,int(t.lightBlock));
    };
    for(int i=0;i<int(count);++i) if(sky[i]>1) {
        bool needed=false;
        neighbours(i,[&](int n) { if(int(sky[i])-lossAt(n)>sky[n])needed=true; });
        if(needed)queue.push_back(i);
    }
    const auto spread=[&](std::vector<uint8_t>& values) {
        for(size_t front=0;front<queue.size();++front) {
            const int i=queue[front],light=values[i];
            if(light<=1)continue;
            neighbours(i,[&](int n) {
                const int candidate=light-lossAt(n);
                if(candidate>values[n]){values[n]=uint8_t(candidate);queue.push_back(n);}
            });
        }
    };
    spread(sky);queue.clear();
    for(int i=0;i<int(count);++i)if(block[i]>1)queue.push_back(i);
    spread(block);++builds;
    return true;
}

namespace {
uint32_t quadTexel(float x, float y, float z, int moonPhase)
{
    if(std::abs(y)<1e-8f)return 0;
    const bool sun=y>0;
    const float half=sun?30.0f:20.0f,t=(sun?100.0f:-100.0f)/y;
    const float qx=x*t,qz=z*t;
    if(std::abs(qx)>half||std::abs(qz)>half)return 0;
    float u=(qx+half)/(2*half),v=(qz+half)/(2*half);
    if(sun) {
        const int ix=std::clamp(int(u*skytextures::kSunWidth),0,skytextures::kSunWidth-1);
        const int iy=std::clamp(int(v*skytextures::kSunHeight),0,skytextures::kSunHeight-1);
        return skytextures::kSunPixels[iy*skytextures::kSunWidth+ix];
    }
    const int phaseIndex=((moonPhase%8)+8)%8;
    const int side=skytextures::kMoonWidth/4;
    const int ix=phaseIndex%4*side+std::clamp(int((1-u)*side),0,side-1);
    const int iy=phaseIndex/4*side+std::clamp(int(v*side),0,side-1);
    return skytextures::kMoonPixels[iy*skytextures::kMoonWidth+ix];
}
}
uint32_t celestialTexel(Point d, float phase, int moonPhase)
{
    if(!std::isfinite(phase)||phase<0)return 0;
    const float a=std::remainder(phase,1.0f)*6.283185307179586f,c=std::cos(a),s=std::sin(a);
    // Inverse original Ry(-90)*Rx(a), not an angular/radial disc test.
    return quadTexel(d.z,c*d.y-s*d.x,-s*d.y-c*d.x,moonPhase);
}
namespace {
Point add(Point a, Point b) { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
Point sub(Point a, Point b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
Point mul(Point a, float s) { return {a.x*s, a.y*s, a.z*s}; }
float dot(Point a, Point b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
Point cross(Point a, Point b) { return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x}; }
void plane(Geometry& g, Point a, Point b, Point c, int face,
           float u=0, float v=1, float uu=1, float uv=0, float vu=0, float vv=-1)
{
    const Point e=sub(b,a), f=sub(c,a), n=cross(e,f);
    const float determinant=dot(n,n);
    if (determinant < 1e-12f) return;
    g.planes.push_back({a,e,f,n,mul(cross(f,n),1/determinant),
                        mul(cross(n,e),1/determinant),u,v,uu,uv,vu,vv,face});
}
void box(Geometry& g, float x0,float y0,float z0,float x1,float y1,float z1)
{ g.boxes.push_back({{x0,y0,z0},{x1,y1,z1}}); }
void doorBox(Geometry& g, int data)
{
    constexpr float r=3/16.0f;
    const int dir=data&3;
    int side=dir;
    // DoorTile::setShape: closed W,N,E,S; open N,E,S,W (left
    // hinge), with the opposite wall for a right hinge.
    if (data&4) side=(dir+((data&16)?3:1))&3;
    if (side==0) box(g,0,0,0,r,1,1);
    if (side==1) box(g,0,0,0,1,1,r);
    if (side==2) box(g,1-r,0,0,1,1,1);
    if (side==3) box(g,0,0,1-r,1,1,1);
}
void faceUV(Point p, int face, float& u, float& v)
{
    u=face<=3?p.x:p.z;
    v=face<=1?p.z:1-p.y;
}
void boxSurfaces(Geometry& out, const Box& b)
{
    const auto l=b.low,h=b.high;
    plane(out,{l.x,l.y,l.z},{h.x,l.y,l.z},{l.x,l.y,h.z},0,l.x,l.z,h.x-l.x,0,0,h.z-l.z);
    plane(out,{l.x,h.y,l.z},{h.x,h.y,l.z},{l.x,h.y,h.z},1,l.x,l.z,h.x-l.x,0,0,h.z-l.z);
    for (int side=0;side<2;++side) {
        const float z=side?h.z:l.z, x=side?h.x:l.x;
        plane(out,{l.x,l.y,z},{h.x,l.y,z},{l.x,h.y,z},2+side,l.x,1-l.y,h.x-l.x,0,0,l.y-h.y);
        plane(out,{x,l.y,l.z},{x,l.y,h.z},{x,h.y,l.z},4+side,l.z,1-l.y,h.z-l.z,0,0,l.y-h.y);
    }
}
}

Geometry buildGeometry(const legacy::BlockSpec& spec)
{
    Geometry g; g.data=spec.data;
    const std::string_view shape=spec.shape;
    if (shape=="cube" || shape=="grass" || shape=="leaves") {
        g.fullCube=true;box(g,0,0,0,1,1,1);return g;
    }
    if (shape=="air") return g;
    if (shape=="stairs") {
        const bool upper=spec.data&4;
        box(g,0,upper?0.5f:0,0,1,upper?1:0.5f,1);
        float x0=0,z0=0,x1=1,z1=1;
        switch (spec.data&3) {
        case 0:x0=.5f;break; case 1:x1=.5f;break;
        case 2:z0=.5f;break; case 3:z1=.5f;break;
        }
        box(g,x0,upper?0:.5f,z0,x1,upper?.5f:1,z1);
    } else if (shape=="slab") {
        const float y=(spec.data&8)?.5f:0;
        box(g,0,y,0,1,y+.5f,1);
    } else if (shape=="door") {
        g.door=true;
        // Upper-only metadata does not contain direction. World traversal
        // replaces this with the adjacent lower half's precomputed geometry.
        doorBox(g,(spec.data&8)?0:spec.data);
    } else if (shape=="fence" || shape=="wall") {
        box(g,.375f,0,.375f,.625f,1,.625f);
        box(g,0,.375f,.4375f,1,.5625f,.5625f);
        box(g,0,.75f,.4375f,1,.9375f,.5625f);
    } else if (shape=="pane") {
        box(g,0,0,.46875f,1,1,.53125f);
        box(g,.46875f,0,0,.53125f,1,1);
    } else if (shape=="ladder" || shape=="vine") {
        const int d=spec.data;
        if (d==4 || d==5) {
            const float x=d==4?.95f:.05f;
            plane(g,{x,0,0},{x,0,1},{x,1,0},d);
        } else {
            const float z=d==3?.05f:.95f;
            plane(g,{0,0,z},{1,0,z},{0,1,z},d==3?3:2);
        }
    } else if (shape=="cross" || shape=="plant" || shape=="crops") {
        if (shape=="crops") {
            for (float a:{.25f,.75f}) {
                plane(g,{a,0,0},{a,0,1},{a,1,0},4);
                plane(g,{0,0,a},{1,0,a},{0,1,a},2);
            }
        } else {
            plane(g,{.05f,0,.05f},{.95f,0,.95f},{.05f,1,.05f},2);
            plane(g,{.05f,0,.95f},{.95f,0,.05f},{.05f,1,.95f},2);
        }
    } else if (shape=="torch") {
        // Cropped source sprite, sheared in the same direction as the original
        // wall torch. All physical vertices remain inside this DDA cell.
        const int d=spec.data;
        const float dx=d==1?-.4f:(d==2?.4f:0), dz=d==3?-.4f:(d==4?.4f:0);
        const float baseY=d>=1&&d<=4?.2f:0;
        const auto p=[=](float x,float y,float z) {
            return Point{x+dx*(1-y)-dx*.25f,y+baseY,z+dz*(1-y)-dz*.25f};
        };
        constexpr float lo=7/16.0f, hi=9/16.0f, h=10/16.0f;
        plane(g,p(lo,0,lo),p(hi,0,lo),p(lo,h,lo),2,lo,1,hi-lo,0,0,-h);
        plane(g,p(lo,0,hi),p(hi,0,hi),p(lo,h,hi),3,lo,1,hi-lo,0,0,-h);
        plane(g,p(lo,0,lo),p(lo,0,hi),p(lo,h,lo),4,lo,1,hi-lo,0,0,-h);
        plane(g,p(hi,0,lo),p(hi,0,hi),p(hi,h,lo),5,lo,1,hi-lo,0,0,-h);
        plane(g,p(lo,h,lo),p(hi,h,lo),p(lo,h,hi),1,lo,6/16.0f,hi-lo,0,0,2/16.0f);
    } else if (shape=="rail" || shape=="wire" || shape=="tripwire" || shape=="lilypad") {
        const int d=spec.data;
        const auto y=[=](float x,float z) {
            // Keep the slope within the cell; the upstream high edge extends
            // 1/16 into the next cell, which a cell-local DDA must not sample.
            if (shape=="rail") {
                if(d==2)return .0625f+x*.9375f;
                if(d==3)return 1-x*.9375f;
                if(d==4)return 1-z*.9375f;
                if(d==5)return .0625f+z*.9375f;
            }
            return shape=="lilypad"?.015625f:.0625f;
        };
        if (shape=="rail" && (d==1 || d==2 || d==3 || d==7))
            plane(g,{0,y(0,1),1},{1,y(1,1),1},{0,y(0,0),0},1,0,0,0,1,1,0);
        else plane(g,{0,y(0,0),0},{1,y(1,0),0},{0,y(0,1),1},1,0,0,1,0,0,1);
    } else if (shape=="chest") {
        box(g,.0625f,0,.0625f,.9375f,.9375f,.9375f);
    } else if (shape=="cactus") {
        box(g,.0625f,0,.0625f,.9375f,1,.9375f);
    } else {
        const float h=shape=="liquid"?.875f:std::clamp(spec.height,0.0f,1.0f);
        box(g,0,0,0,1,h,1);
    }
    return g;
}

bool rayIntersection(const Geometry& g, Point origin, Point direction,
                     float nearDistance, float farDistance, Hit& hit)
{
    bool found=false;
    const auto inside=[&](Point p) {
        for (const auto& b:g.boxes)
            if(p.x>b.low.x+1e-6f&&p.x<b.high.x-1e-6f&&p.y>b.low.y+1e-6f&&
               p.y<b.high.y-1e-6f&&p.z>b.low.z+1e-6f&&p.z<b.high.z-1e-6f) return true;
        return false;
    };
    for (const auto& b:g.boxes) {
        float enter=-1e30f,leave=1e30f; int entering=0,leaving=0;
        const auto slab=[&](float o,float d,float lo,float hi,int lowFace,int highFace) {
            if(std::abs(d)<1e-8f)return o>=lo&&o<=hi;
            float a=(lo-o)/d,c=(hi-o)/d; int af=lowFace,cf=highFace;
            if(a>c){std::swap(a,c);std::swap(af,cf);}
            if(a>enter){enter=a;entering=af;} if(c<leave){leave=c;leaving=cf;}
            return enter<=leave;
        };
        if(!slab(origin.x,direction.x,b.low.x,b.high.x,4,5)||
           !slab(origin.y,direction.y,b.low.y,b.high.y,0,1)||
           !slab(origin.z,direction.z,b.low.z,b.high.z,2,3))continue;
        for(int side=0;side<2;++side) {
            const float t=side?leave:enter; const int face=side?leaving:entering;
            if(t<nearDistance-1e-5f||t>farDistance+1e-5f)continue;
            // Ignore seams inside the union, but allow later exit surfaces
            // after a transparent entry texel in this same cell.
            if(inside(add(origin,mul(direction,t+(side?1e-4f:-1e-4f)))))continue;
            float u,v;faceUV(add(origin,mul(direction,t)),face,u,v);
            hit={t,u,v,face};farDistance=t;found=true;
        }
    }
    for (const auto& p:g.planes) {
        const float denominator=dot(p.normal,direction);
        if(std::abs(denominator)<1e-8f)continue;
        const float t=dot(p.normal,sub(p.origin,origin))/denominator;
        if(t<nearDistance-1e-5f||t>farDistance+1e-5f)continue;
        const Point q=sub(add(origin,mul(direction,t)),p.origin);
        const float a=dot(q,p.dualU),b=dot(q,p.dualV);
        if(a<0||a>1||b<0||b>1)continue;
        hit={t,p.u+a*p.uu+b*p.vu,p.v+a*p.uv+b*p.vv,p.face};farDistance=t;found=true;
    }
    return found;
}

std::vector<legacy::Vertex> buildShapeMesh(const legacy::BlockSpec& spec, int compositeDoorData)
{
    Geometry g=buildGeometry(spec);
    if(g.door&&compositeDoorData>=0){g.boxes.clear();doorBox(g,compositeDoorData);}
    for(const auto& b:g.boxes)boxSurfaces(g,b);
    legacy::IconRegister registry;
    std::vector<legacy::Vertex> result;
    for(const auto& p:g.planes) {
        const auto* tex=registry.registerIcon(spec.faces[p.face]);
        const float shade=p.face==1?1:(p.face==0?.5f:(p.face<=3?.8f:.6f));
        const float r=((spec.color>>16)&255)/255.0f*shade;
        const float green=((spec.color>>8)&255)/255.0f*shade, b=(spec.color&255)/255.0f*shade;
        const auto vertex=[&](float a,float c) {
            const Point q=add(p.origin,add(mul(p.edgeU,a),mul(p.edgeV,c)));
            float u=p.u+a*p.uu+c*p.vu;
            // Match DoorTile::getTexture's horizontal flip selection.
            const int data=compositeDoorData>=0?compositeDoorData:((spec.data&8)?0:spec.data);
            if(g.door&&p.face>=2) {
                const int dir=data&3;
                bool flip=(data&4)?p.face==std::array<int,4>{2,5,3,4}[dir]
                                       :p.face==std::array<int,4>{5,3,4,2}[dir];
                if(!(data&4)&&(data&16))flip=!flip;
                if(flip)u=1-u;
            }
            result.push_back({q.x,q.y,q.z,tex->u0+(tex->u1-tex->u0)*u,
                tex->v0+(tex->v1-tex->v0)*(p.v+a*p.uv+c*p.vv),r,green,b,spec.alpha});
        };
        vertex(0,0);vertex(1,0);vertex(1,1);vertex(0,1);
    }
    return result;
}
} // namespace worldrender

namespace {
using Vector = worldrender::Point;
struct Texel { float r, g, b, alpha; float tintR = 0, tintG = 0, tintB = 0; };
struct Face {
    // Keep atlas UV arithmetic: rounding at exact texel boundaries must match
    // the old sampler, not just int(u * 16) (the atlas has ten rows).
    float u0, v0, du, dv;
    int x0, y0;
    int tintKind = 0;
    std::array<Texel, 16 * 16> texels;
};
using BlockFaces = std::array<Face, 6>;

const std::vector<BlockFaces>& textures()
{
    static const auto result = [] {
        std::vector<BlockFaces> sets(legacy::kBlockCount);
        legacy::IconRegister registry;
        const auto* overlay = registry.registerIcon(L"grass_side_overlay");
        const int overlayX = int(overlay->u0 * legacy::kAtlasWidth);
        const int overlayY = int(overlay->v0 * legacy::kAtlasHeight);
        for (int index = 0; index < legacy::kBlockCount; ++index) {
            const auto& block = legacy::kBlockSpecs[index];
            const bool grass = std::string_view(block.shape) == "grass";
            const bool water = std::string_view(block.key) == "water" || std::string_view(block.key) == "calmWater";
            for (int face = 0; face < 6; ++face) {
                const auto* icon = registry.registerIcon(block.faces[face]);
                auto& output = sets[index][face];
                output.u0 = icon->u0; output.v0 = icon->v0;
                output.du = icon->u1 - icon->u0; output.dv = icon->v1 - icon->v0;
                output.x0 = int(icon->u0 * legacy::kAtlasWidth);
                output.y0 = int(icon->v0 * legacy::kAtlasHeight);
                output.tintKind = grass ? 1 : (block.tileId == 18 && (block.data & 3) == 0 ? 2 : 0);
                float tintR = ((block.color >> 16) & 255) / 255.0f;
                float tintG = ((block.color >> 8) & 255) / 255.0f;
                float tintB = (block.color & 255) / 255.0f;
                if (grass && face != 1) tintR = tintG = tintB = 1;
                const float shade = face == 1 ? 1.0f : (face == 0 ? 0.5f : (face <= 3 ? 0.82f : 0.65f));
                for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) {
                    const uint32_t pixel = legacy::kAtlasPixels[(output.y0 + y) * legacy::kAtlasWidth + output.x0 + x];
                    float alpha = (pixel >> 24) / 255.0f;
                    float r = ((pixel >> 16) & 255) * tintR;
                    float g = ((pixel >> 8) & 255) * tintG;
                    float b = (pixel & 255) * tintB;
                    if (grass && face >= 2) {
                        const uint32_t top = legacy::kAtlasPixels[(overlayY + y) * legacy::kAtlasWidth + overlayX + x];
                        const float a = (top >> 24) / 255.0f;
                        r = r * (1 - a) + ((top >> 16) & 255) * ((block.color >> 16) & 255) / 255.0f * a;
                        g = g * (1 - a) + ((top >> 8) & 255) * ((block.color >> 8) & 255) / 255.0f * a;
                        b = b * (1 - a) + (top & 255) * (block.color & 255) / 255.0f * a;
                    }
                    // Preserve the texture-alpha rejection BEFORE block alpha.
                    alpha = alpha < 0.1f ? 0.0f : alpha * (water ? 0.55f : block.alpha);
                    float tr = 0, tg = 0, tb = 0;
                    if (output.tintKind == 2 || (grass && face == 1)) {
                        tr = ((pixel >> 16) & 255) * shade;
                        tg = ((pixel >> 8) & 255) * shade;
                        tb = (pixel & 255) * shade;
                    } else if (grass && face >= 2) {
                        const uint32_t top = legacy::kAtlasPixels[(overlayY + y) * legacy::kAtlasWidth + overlayX + x];
                        const float overlayAlpha = (top >> 24) / 255.0f;
                        tr = ((top >> 16) & 255) * shade * overlayAlpha;
                        tg = ((top >> 8) & 255) * shade * overlayAlpha;
                        tb = (top & 255) * shade * overlayAlpha;
                    }
                    output.texels[y * 16 + x] = {r * shade, g * shade, b * shade, alpha, tr, tg, tb};
                }
            }
        }
        return sets;
    }();
    return result;
}

struct CachedGeometry {
    worldrender::Geometry shape;
    int doorFamily;
};
const std::vector<CachedGeometry>& geometries()
{
    static const auto result=[] {
        std::vector<CachedGeometry> result;
        result.reserve(legacy::kBlockCount);
        for(const auto& spec:legacy::kBlockSpecs) {
            auto shape=worldrender::buildGeometry(spec);
            const int family=shape.door?(std::string_view(spec.key).find("iron")!=std::string_view::npos?2:1):0;
            result.push_back({std::move(shape),family});
        }
        return result;
    }();
    return result;
}
const std::array<worldrender::Geometry,16>& doorGeometries()
{
    static const auto result=[] {
        std::array<worldrender::Geometry,16> result;
        for(int i=0;i<16;++i)worldrender::doorBox(result[i],(i&7)|((i&8)?16:0));
        return result;
    }();
    return result;
}
float fraction(float value) { return value - std::floor(value); }
struct Axis {
    int step;
    float delta, first;
};
Axis axis(float direction, int cell, float origin)
{
    const int step = direction < 0 ? -1 : 1;
    const float delta = std::abs(direction) > 0.000001f ? std::abs(1 / direction) : 1e20f;
    return {step, delta, (step > 0 ? cell + 1 - origin : origin - cell) * delta};
}
struct View {
    const uint16_t* voxels;
    const BlockFaces* sets;
    const CachedGeometry* geometry;
    const worldrender::Geometry* doors;
    Vector eye;
    int x, y, z;
    int originX, originY, originZ, width, height, depth;
    float daylight;
    float viewDistance;
    float phaseSin, phaseCos;
    bool celestial;
    int moonPhase;
    const worldrender::BiomeTint* tints;
    const worldrender::LightingBuffer* lighting;
    const worldrender::TileLighting* metadata;
    bool ao;
    std::array<float,16> skyRamp, blockRamp;
};
struct Row {
    float upX, upZ, directionY;
    Axis y;
    float skyR, skyG;
};
float surfaceLight(const View& view, int x, int y, int z, int face, Vector p, bool fullCube, uint16_t voxel)
{
    if(!view.lighting)return view.daylight;
    // Original special/inset faces sample their own cell, exterior full-cube
    // faces sample the adjacent air. Never call World::blockAt in a pixel loop.
    int nx=x,ny=y,nz=z;
    const float coord=face<2?p.y:(face<4?p.z:p.x);
    const bool exterior=(face%2)?coord>.9999f:coord<.0001f;
    if(fullCube||exterior) {
        if(face<2)ny+=face==0?-1:1;
        else if(face<4)nz+=face==2?-1:1;
        else nx+=face==4?-1:1;
    }
    auto sample=view.lighting->sample(nx,ny,nz);
    // Tile::propagate / Level::getBrightness neighbour-brightness subset:
    // stairs, slabs and farmland block light as whole cells but their inset
    // surfaces must take max(above, +/-X, +/-Z), not a black solid-cell value.
    if(!fullCube&&!exterior&&view.metadata[voxel].lightBlock>=15) {
        const auto merge=[&](int a,int b,int c) {
            const auto neighbour=view.lighting->sample(a,b,c);
            sample.sky=std::max(sample.sky,neighbour.sky);
            sample.block=std::max(sample.block,neighbour.block);
        };
        merge(x,y+1,z);merge(x-1,y,z);merge(x+1,y,z);merge(x,y,z-1);merge(x,y,z+1);
    }
    const int emission=view.metadata[voxel].emission;
    const float sourceShade=face<=1?1.0f:(face<=3?.8f/.82f:.6f/.65f);
    const float light=sourceShade*std::max(view.skyRamp[sample.sky],view.blockRamp[std::max(int(sample.block),emission)]);
    if(!view.ao||!fullCube||emission)return light;
    // TileRenderer AO subset: face-centre + two side neighbours + diagonal,
    // using 0.2 for solid blocking and 1 otherwise, bilinear across the face.
    const auto occlusion=[&](int a,int b,int c) {
        if(unsigned(a-view.originX)>=unsigned(view.width)||unsigned(b-view.originY)>=unsigned(view.height)||
           unsigned(c-view.originZ)>=unsigned(view.depth))return 1.0f;
        // [FIX 5] Unloaded columns are "unknown": they neither occlude nor
        // expose, so they must not darken (or brighten) the border. Treat
        // them like the outside window so AO agrees with sample().
        if(view.lighting && view.lighting->loadedColumns &&
           !view.lighting->loadedColumns[(c-view.originZ)*view.width+(a-view.originX)]) return 1.0f;
        const int i=((b-view.originY)*view.depth+c-view.originZ)*view.width+a-view.originX;
        return view.metadata[view.voxels[i]].occludesAO?.2f:1.0f;
    };
    const float u=std::clamp(face<4?p.x:p.z,0.0f,1.0f);
    const float v=std::clamp(face<2?p.z:p.y,0.0f,1.0f);
    const auto corner=[&](int a,int b) {
        const int ax=face<4?a:0,az=face<4?0:a;
        const int by=face<2?0:b,bz=face<2?b:0;
        const float sideA=occlusion(nx+ax,ny,nz+az),sideB=occlusion(nx,ny+by,nz+bz);
        const float diagonal=sideA<1&&sideB<1?sideA:occlusion(nx+ax,ny+by,nz+az+bz);
        return (1+sideA+sideB+diagonal)*.25f;
    };
    const float low=corner(-1,-1)*(1-u)+corner(1,-1)*u;
    const float high=corner(-1,1)*(1-u)+corner(1,1)*u;
    return light*(low*(1-v)+high*v);
}
uint32_t trace(const View& view, const Row& row, float directionX, float directionZ)
{
    const Axis ax = axis(directionX, view.x, view.eye.x), az = axis(directionZ, view.z, view.eye.z);
    int x = view.x, y = view.y, z = view.z;
    int cell = ((y - view.originY) * view.depth + z - view.originZ) * view.width + x - view.originX;
    const int stepY = row.y.step * view.width * view.depth;
    const int stepZ = az.step * view.width;
    const float rayLength = std::sqrt(directionX * directionX + row.directionY * row.directionY + directionZ * directionZ);
    const float maxDistance = view.viewDistance / rayLength;
    float tx = ax.first, ty = row.y.first, tz = az.first;
    float red = 0, green = 0, blue = 0, transmission = 1;
    // [FIX 3a] Track whether the ray left the resident window or hit an
    // unloaded column. Those pixels are "unknown", not "sky".
    bool hitBoundary = false;
    // The original fullcube traversal starts at the first crossed boundary.
    // Only a special shape in the eye cell needs an initial local intersection.
    const bool inBounds=unsigned(x-view.originX)<unsigned(view.width)&&
        unsigned(y-view.originY)<unsigned(view.height)&&unsigned(z-view.originZ)<unsigned(view.depth);
    const uint16_t initial=inBounds?view.voxels[cell]:0;
    const bool initialSpecial=initial&&initial<=legacy::kBlockCount&&!view.geometry[initial-1].shape.fullCube;
    for (int step = 0; step < 180; ++step) {
        int face=0;
        float distance=0;
        if(step==0&&initialSpecial) {
        } else if (tx < ty && tx < tz) {
            distance = tx; tx += ax.delta; x += ax.step; cell += ax.step; face = ax.step > 0 ? 4 : 5;
        } else if (ty < tz) {
            distance = ty; ty += row.y.delta; y += row.y.step; cell += stepY; face = row.y.step > 0 ? 0 : 1;
        } else {
            distance = tz; tz += az.delta; z += az.step; cell += stepZ; face = az.step > 0 ? 2 : 3;
        }
        // Check coordinates before indexing; a linear index alone can wrap
        // across x/z world faces and still look in bounds.
        // [FIX 3b] Far-distance miss is still a normal sky fallthrough.
        if (distance > maxDistance) break;
        // [FIX 3c] Leaving the resident window is "unknown", not air.
        if (unsigned(x - view.originX) >= unsigned(view.width) ||
            unsigned(z - view.originZ) >= unsigned(view.depth) ||
            unsigned(y - view.originY) >= unsigned(view.height)) {
            hitBoundary = true; break;
        }
        // [FIX 3d] A non-resident column is padding, not air. If a zero
        // voxel lands in an unloaded column, treat the hit as boundary.
        if (view.lighting && view.lighting->loadedColumns &&
            !view.lighting->loadedColumns[(z - view.originZ) * view.width + (x - view.originX)]) {
            hitBoundary = true; break;
        }
        const uint16_t voxel = view.voxels[cell];
        if (!voxel || voxel > legacy::kBlockCount) continue;
        float u, v;
        const auto& cached=view.geometry[voxel-1];
        const bool special=!cached.shape.fullCube;
        const worldrender::Geometry* geometry=&cached.shape;
        int doorData=0;
        if(cached.shape.door) {
            const bool upper=cached.shape.data&8;
            const int neighborY=upper?y-1:y+1;
            const int offset=upper?-view.width*view.depth:view.width*view.depth;
            // Never read the adjacent row until the absolute window guard has
            // passed; an upper half at the window floor has no lower context.
            const uint16_t neighbor=unsigned(neighborY-view.originY)<unsigned(view.height)?view.voxels[cell+offset]:0;
            const CachedGeometry* other=neighbor&&neighbor<=legacy::kBlockCount?&view.geometry[neighbor-1]:nullptr;
            const bool paired=other&&other->doorFamily==cached.doorFamily&&
                bool(other->shape.data&8)!=upper;
            // Direction is absent from upper metadata. Only isolated PREVIEW
            // may invent direction 0; a world upper with no lower context is
            // omitted rather than rendered in a misleading orientation.
            if(upper&&!paired)continue;
            const int lower=upper?other->shape.data:cached.shape.data;
            const int upperData=upper?cached.shape.data:(paired?other->shape.data:8);
            doorData=(lower&7)|((upperData&1)?16:0);
            geometry=&view.doors[(lower&7)|((upperData&1)?8:0)];
        }
        const Vector localOrigin{view.eye.x-x,view.eye.y-y,view.eye.z-z};
        const Vector direction{directionX,row.directionY,directionZ};
        const float cellEnd=std::min({tx,ty,tz,maxDistance});
        float nextHit=distance;
        // Cutout planes and multiple disjoint boxes can have another visible
        // surface inside the SAME cell. A miss/transparent texel must not turn
        // into a cube hit or skip an interior stair riser.
        do {
        if(special) {
            worldrender::Hit hit;
            if(!worldrender::rayIntersection(*geometry,localOrigin,direction,nextHit,cellEnd,hit))break;
            distance=hit.distance;face=hit.face;u=hit.u;v=hit.v;
            nextHit=distance+0.0001f;
            if(cached.shape.door&&face>=2) {
                const int dir=doorData&3;
                bool flip=(doorData&4)?face==std::array<int,4>{2,5,3,4}[dir]
                                      :face==std::array<int,4>{5,3,4,2}[dir];
                if(!(doorData&4)&&(doorData&16))flip=!flip;
                if(flip)u=1-u;
            }
        } else if (face <= 1) {
            u = fraction(view.eye.x + directionX * distance);
            v = fraction(view.eye.z + directionZ * distance);
        } else {
            u = face <= 3 ? fraction(view.eye.x + directionX * distance)
                          : fraction(view.eye.z + directionZ * distance);
            v = 1 - fraction(view.eye.y + row.directionY * distance);
        }
        const Face& texture = view.sets[voxel - 1][face];
        const float tu = texture.u0 + texture.du * std::clamp(u, 0.0001f, 0.9999f);
        const float tv = texture.v0 + texture.dv * std::clamp(v, 0.0001f, 0.9999f);
        const int ix = std::clamp(int(tu * legacy::kAtlasWidth) - texture.x0, 0, 15);
        const int iy = std::clamp(int(tv * legacy::kAtlasHeight) - texture.y0, 0, 15);
        const Texel& texel = texture.texels[iy * 16 + ix];
        if (texel.alpha == 0) continue;
        float baseR = texel.r, baseG = texel.g, baseB = texel.b;
        if (view.tints && texture.tintKind) {
            const auto& tint = view.tints[(z - view.originZ) * view.width + x - view.originX];
            const auto color = legacy::kBlockSpecs[voxel - 1].color;
            const float r = texture.tintKind == 1 ? tint.grassR : tint.leafR;
            const float g = texture.tintKind == 1 ? tint.grassG : tint.leafG;
            const float b = texture.tintKind == 1 ? tint.grassB : tint.leafB;
            baseR += texel.tintR * (r - ((color >> 16) & 255) / 255.0f);
            baseG += texel.tintG * (g - ((color >> 8) & 255) / 255.0f);
            baseB += texel.tintB * (b - (color & 255) / 255.0f);
        }
        const float fogStart = view.viewDistance * (23.0f / 40.0f);
        const float fog = std::clamp((distance * rayLength - fogStart) /
                                     (view.viewDistance - fogStart), 0.0f, 0.9f);
        const Vector localHit{localOrigin.x+directionX*distance,localOrigin.y+row.directionY*distance,
                              localOrigin.z+directionZ*distance};
        const float light=surfaceLight(view,x,y,z,face,localHit,!special,voxel);
        const float r = baseR * light * (1 - fog) + row.skyR * fog;
        const float g = baseG * light * (1 - fog) + row.skyG * fog;
        const float b = baseB * light * (1 - fog) + 239 * view.daylight * fog;
        red += transmission * texel.alpha * r;
        green += transmission * texel.alpha * g;
        blue += transmission * texel.alpha * b;
        transmission *= 1 - texel.alpha;
        if (transmission < 0.02f) break;
        } while(special&&nextHit<=cellEnd);
        if (transmission < 0.02f) break;
    }
    // [FIX 3e] Boundary fade: the ray reached the resident border or an
    // unloaded column. That pixel is "unknown", not "sky". Fade to fog and
    // suppress the celestial layer so no sun/moon is drawn behind missing
    // terrain. This is what eliminates the border "ghost block" holes.
    if (hitBoundary) {
        const float fog = 0.9f;
        red   = red   * (1 - fog) + row.skyR * fog;
        green = green * (1 - fog) + row.skyG * fog;
        blue  = blue  * (1 - fog) + 239.0f * view.daylight * fog;
        transmission = 0.0f;
    }
    if (transmission > 0.02f && view.celestial) {
        const uint32_t texel=worldrender::quadTexel(directionZ,
            view.phaseCos*row.directionY-view.phaseSin*directionX,
            -view.phaseSin*row.directionY-view.phaseCos*directionX,view.moonPhase);
        // Original GL_SRC_ALPHA, GL_ONE: black texture padding adds nothing,
        // opaque coloured pixels do NOT replace the background sky.
        const float alpha=(texel>>24)/255.0f;
        red+=transmission*alpha*((texel>>16)&255);
        green+=transmission*alpha*((texel>>8)&255);
        blue+=transmission*alpha*(texel&255);
    }
    return ARGB(255, std::clamp(int(red + transmission * row.skyR), 0, 255),
                     std::clamp(int(green + transmission * row.skyG), 0, 255),
                     std::clamp(int(blue + transmission * 239 * view.daylight), 0, 255));
}
struct Column { float x, z; };
}

void worldrender::renderVolumeFrame(uint32_t* pixels, int width, int height,
                                    const mcworld::RenderVolume& volume, Point eye,
                                    float yaw, float pitch, int pixelStep,
                                    float daylight, float timeOfDay, const BiomeTint* tints,
                                    const LightingSettings& lighting)
{
    if (!pixels || width <= 0 || height <= 0 || !volume.data ||
        volume.width<=0 || volume.height<=0 || volume.depth<=0 ||
        uint64_t(volume.width)*volume.height*volume.depth>uint64_t(std::numeric_limits<int>::max()) ||
        !std::isfinite(eye.x)||!std::isfinite(eye.y)||!std::isfinite(eye.z)||
        !std::isfinite(yaw) || !std::isfinite(pitch) ||
        !std::isfinite(daylight) || !std::isfinite(timeOfDay)) return;
    // Production eyes are inside the resident window. Do not allow an outside
    // origin to overflow its linear index or wrap across a sparse window edge.
    if(eye.x<volume.originX||eye.x>=float(volume.originX)+volume.width||
       eye.y<volume.originY||eye.y>=float(volume.originY)+volume.height||
       eye.z<volume.originZ||eye.z>=float(volume.originZ)+volume.depth)return;
    daylight = std::clamp(daylight, 0.05f, 1.0f);
    constexpr float radians = 3.14159265358979323846f / 180;
    yaw = std::remainder(yaw, 360.0f) * radians;
    pitch = std::clamp(pitch, -80.0f, 80.0f) * radians;
    const float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
    const Vector forward{sy * cp, sp, cy * cp}, right{cy, 0, -sy}, up{-sy * sp, cp, -cy * sp};
    if(lighting.enabled) {
        // Tell the cache where the player is looking so an incremental rebuild
        // starts with the columns in view.
        renderLighting.viewEyeX=eye.x; renderLighting.viewEyeZ=eye.z;
        renderLighting.viewDirX=forward.x; renderLighting.viewDirZ=forward.z;
        renderLighting.viewValid=true;
        renderLighting.update(volume,lighting.cachePath,lighting.revision);
    }
    const float phaseAngle=std::remainder(timeOfDay,1.0f)*6.283185307179586f;
    View view{volume.data, textures().data(), geometries().data(), doorGeometries().data(), eye,
                    int(std::floor(eye.x)), int(std::floor(eye.y)), int(std::floor(eye.z)),
                    volume.originX, volume.originY, volume.originZ,
                    volume.width, volume.height, volume.depth, daylight,
                    std::clamp(lighting.maxDistance, 1.0f, 40.0f),
                    std::sin(phaseAngle),std::cos(phaseAngle),timeOfDay>=0,lighting.moonPhase,tints,
                    lighting.enabled?&renderLighting:nullptr,worldrender::catalogLighting().data(),
                    lighting.ambientOcclusion,{},{}};
    // [FIX 4] Pull the effective view distance in to the nearest window face.
    // Rays then expire a little before the border and take the fog from
    // trace() instead of reaching for the sky past unloaded columns. If the
    // eye is closer than 2 blocks to a face, leave the distance alone and
    // rely on trace()'s boundary fade.
    {
        const float edge = std::min({
            eye.x - float(volume.originX),
            float(volume.originX) + float(volume.width)  - eye.x,
            eye.y - float(volume.originY),
            float(volume.originY) + float(volume.height) - eye.y,
            eye.z - float(volume.originZ),
            float(volume.originZ) + float(volume.depth)  - eye.z
        });
        if (edge > 2.0f) view.viewDistance = std::min(view.viewDistance, edge - 1.0f);
    }
    // Source Dimension brightness ramp, scalar display approximation. Daylight
    // affects only skylight: torches keep their intensity at night.
    for(int level=0;level<16;++level) {
        const float v=1-level/15.0f;
        const float ramp=(1-v)/(v*3+1);
        view.skyRamp[level]=.035f+.965f*ramp*daylight;
        view.blockRamp[level]=.035f+.965f*ramp;
    }
    const float projectionScale=lighting.legacyProjection?1.0f:width/(2*focalLength(width,height));
    const int stride = std::clamp(pixelStep, 1, std::max(width, height));
    // Reuse storage between frames. Preserve the original operation order (no
    // incrementally accumulated directions), including exact grid crossings.
    thread_local std::vector<Column> columns;
    columns.resize(size_t((width - 1) / stride) + 1);
    for (size_t column = 0; column < columns.size(); ++column) {
        const int x = int(column * size_t(stride));
        const float vx = ((x + 1.0f) / width * 2 - 1)*projectionScale;
        columns[column] = {forward.x + right.x * vx, forward.z + right.z * vx};
    }
    for (int y = 0; y < height;) {
        const float vy = (1 - (y + 1.0f) / height * 2) * height / width*projectionScale;
        const float directionY = forward.y + up.y * vy;
        const float skyMix = std::clamp(0.5f + directionY * 0.35f, 0.0f, 1.0f);
        const Row row{up.x * vy, up.z * vy, directionY, axis(directionY, view.y, eye.y),
                      (150 - 45 * skyMix) * daylight, (204 - 34 * skyMix) * daylight};
        const int tileHeight = std::min(stride, height - y);
        uint32_t* output = pixels + size_t(y) * width;
        for (size_t column = 0; column < columns.size(); ++column) {
            const int x = int(column * size_t(stride));
            const auto& base = columns[column];
            const uint32_t color = trace(view, row, base.x + row.upX, base.z + row.upZ);
            std::fill_n(output + x, std::min(stride, width - x), color);
        }
        // Trace each tile once, then copy its completed scanline, including the
        // clipped right/bottom tiles. No temporary framebuffer is needed.
        for (int oy = 1; oy < tileHeight; ++oy)
            std::copy_n(output, width, output + size_t(oy) * width);
        y += tileHeight;
    }
    const int cx = width / 2, centerY = height / 2;
    for (int offset = -4; offset <= 4; ++offset) {
        if (cx + offset >= 0 && cx + offset < width) pixels[size_t(centerY) * width + cx + offset] = 0xffeeeeee;
        if (centerY + offset >= 0 && centerY + offset < height) pixels[size_t(centerY + offset) * width + cx] = 0xffeeeeee;
    }
}

void renderWorldFrame(uint32_t* pixels, int width, int height,
                      const mcworld::World& world, float yaw, float pitch, int pixelStep,
                      float daylight, float timeOfDay, const worldrender::LightingSettings& lighting)
{
    if (!pixels || width <= 0 || height <= 0 || !std::isfinite(yaw) || !std::isfinite(pitch) ||
        !std::isfinite(daylight) || !world.loaded()) return;
    const auto& volume = world.renderVolume();
    struct TintCache {
        std::string path;
        int x = -1, z = -1, width = 0, depth = 0;
        std::vector<worldrender::BiomeTint> values;
    };
    thread_local TintCache cache;
    const worldrender::BiomeTint* tints = nullptr;
    if (world.originalGeneration()) {
        if (cache.path != world.path() || cache.x != volume.originX || cache.z != volume.originZ ||
            cache.width != volume.width || cache.depth != volume.depth) {
            cache.values.resize(size_t(volume.width) * volume.depth);
            for (int z = 0; z < volume.depth; ++z) for (int x = 0; x < volume.width; ++x) {
                const uint32_t grass = world.grassColorAt(volume.originX + x, volume.originZ + z);
                const uint32_t leaves = world.foliageColorAt(volume.originX + x, volume.originZ + z);
                cache.values[size_t(z) * volume.width + x] = {
                    ((grass >> 16) & 255) / 255.0f, ((grass >> 8) & 255) / 255.0f, (grass & 255) / 255.0f,
                    ((leaves >> 16) & 255) / 255.0f, ((leaves >> 8) & 255) / 255.0f, (leaves & 255) / 255.0f};
            }
            cache.path = world.path(); cache.x = volume.originX; cache.z = volume.originZ;
            cache.width = volume.width; cache.depth = volume.depth;
        }
        tints = cache.values.data();
    }
    auto settings=lighting;
    settings.cachePath=world.path();settings.revision=world.revision();
    if (world.streaming()) settings.maxDistance = float(world.streamingInfo().maxDistance);
    if (settings.enabled) {
        const auto edit = world.lastEdit();
        auto& cache = const_cast<worldrender::LightingBuffer&>(worldrender::lightingBuffer());
        if (cache.path != settings.cachePath) {
            cache.patchedEditRevision = 0;
            cache.patchedViewRevision = 0;
        }
        // A local response does not certify the rest of the window. Leave the
        // completed revision alone so the budgeted full pass still converges.
        if (edit.revision && cache.patchedEditRevision != edit.revision && cache.path == settings.cachePath &&
            cache.refreshAround(volume, edit.x, edit.y, edit.z))
            cache.patchedEditRevision = edit.revision;
        const int viewYaw = int(std::floor(std::remainder(yaw, 360.0f) / 15.0f));
        const int viewPitch = int(std::floor(pitch / 15.0f));
        if (cache.path == settings.cachePath && cache.revision != settings.revision &&
            (cache.patchedViewRevision != settings.revision || cache.patchedViewYaw != viewYaw ||
             cache.patchedViewPitch != viewPitch)) {
            const auto hit = world.pick(yaw, pitch, 5.0f);
            const float angle = std::remainder(yaw, 360.0f) * .017453292519943295f;
            const int x = hit.hit ? hit.x : int(std::floor(world.playerX() + std::sin(angle) * 4));
            const int y = hit.hit ? hit.y : int(std::floor(world.playerY() + 1.62f));
            const int z = hit.hit ? hit.z : int(std::floor(world.playerZ() + std::cos(angle) * 4));
            if (world.chunkLoaded(x, z) && cache.refreshAround(volume, x, y, z, 4)) {
                cache.patchedViewRevision = settings.revision;
                cache.patchedViewYaw = viewYaw; cache.patchedViewPitch = viewPitch;
            }
        }
    }
    worldrender::renderVolumeFrame(pixels,width,height,volume,
        {world.playerX(),world.playerY()+1.62f,world.playerZ()},yaw,pitch,pixelStep,daylight,timeOfDay,tints,settings);
}