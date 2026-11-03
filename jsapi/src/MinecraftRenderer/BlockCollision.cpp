#include "BlockCollision.hpp"
#include "ItemCatalog.hpp"
#include "LegacyCatalog.hpp"

#include <array>

namespace mcworld {
CollisionState collisionState(uint16_t block) {
    // Render indices map to source state once, not an O(catalog) scan per query.
    static const auto states = [] {
        std::array<CollisionState, legacy::kBlockCount + 1> result {};
        for (auto& entry : result) entry = {-1, -1};
        result[0] = {0, 0};
        for (int i = 0; i < sourceitems::blockStateCount(); ++i) {
            const auto& state = sourceitems::blockStates()[i];
            if (state.renderIndex >= 0 && state.renderIndex < legacy::kBlockCount)
                result[size_t(state.renderIndex) + 1] = {state.tileId, state.data};
        }
        return result;
    }();
    return block < states.size() ? states[block] : CollisionState {-1, -1};
}
bool collisionLadder(CollisionState state) { return state.tileId == 65 || state.tileId == 106; }
bool collisionIntersects(const CollisionBox& a, const CollisionBox& b) {
    return a.x1 > b.x0 && a.x0 < b.x1 && a.y1 > b.y0 && a.y0 < b.y1 && a.z1 > b.z0 && a.z0 < b.z1;
}
namespace {
bool stairs(int tile) {
    switch (tile) {
    // 108/135/136/156 are brick/birch/jungle/quartz stairs: the same StairTile
    // composite shape, so they reuse the identical neighbor-locked boxes.
    case 53: case 67: case 108: case 109: case 114: case 128: case 134:
    case 135: case 136: case 156: return true;
    default: return false;
    }
}
// Audited source cube/material subset for fence attachment, not solidity of
// collision shapes. In particular glass/leaves do not attach like stone.
bool blockingCube(int tile) {
    switch (tile) {
    case 1: case 2: case 3: case 4: case 5: case 7: case 12: case 13:
    case 14: case 15: case 16: case 17: case 21: case 23: case 24: case 35:
    case 43: case 47: case 48: case 49: case 52: case 56: case 58:
    case 61: case 73: case 74: case 80: case 82: case 87: case 88:
    case 97: case 98: case 99: case 100: case 110: case 112: case 121:
    case 129: case 153: return true;
    // Static blocks completed in this pass that are solid-blocking cubes, so
    // they both collide as a full cell and satisfy fence/wall attachment.
    // Deliberately excluded: 95 stained glass (glass material is never
    // buildable, so it does not attach) and 122 dragon egg / 145 anvil /
    // 116 enchantment table (not cube-shaped), even though those still have
    // blocking collision. 123/124 redstone lamps DO attach, unlike plain
    // glass, because their material is the buildable glass variant.
    case 19: case 22: case 41: case 42: case 45: case 57: case 123: case 124:
    case 133: case 152: case 155: case 159: case 170: case 172: case 173:
        return true;
    default: return false;
    }
}
// Vine attachment. The source accepts a neighbour that is BOTH cube-shaped and
// motion-blocking, which is a strictly wider test than the fence/wall predicate:
// fences additionally require the material to be buildable and reject the
// vegetable material, so glass, leaves, ice, TNT, stained glass and pumpkins
// hold a vine even though they never connect a fence post. Non-cube shapes
// (slabs, stairs, snow layers, farmland, cactus, chests, panes, another vine)
// are rejected on both paths. Independent implementation, behaviour verified
// against observed game rules.
bool vineAttachable(int tile) {
    if (tile <= 0) return false;
    if (blockingCube(tile)) return true;
    switch (tile) {
    // Cube-shaped, motion-blocking, but not fence-attachable: leaves and ice are
    // never buildable, TNT is explosive, glass/stained glass/glowstone-family are
    // the glass materials, pumpkin/melon are the vegetable material.
    case 18: case 20: case 46: case 79: case 86: case 89: case 95: case 103:
    case 123: case 124: return true;
    // Deliberately NOT attachable: 44/126 slabs and every stair id are not
    // cube-shaped; 78 snow layer, 60 farmland, 81 cactus, 54 chest, 101/102/160
    // panes, 85/113/139 fences and walls, 171 carpet, 116 enchantment table,
    // 145 anvil, 65 ladder and 106 vine itself all override isCubeShaped.
    default: return false;
    }
}
}
bool collisionClimbable(CollisionState s, int x, int y, int z, const CollisionContext& context) {
    if (s.tileId == 65) return true; // LadderTile keeps its face in data; see header.
    if (s.tileId != 106) return false;
    auto neighbour = [&](int dx, int dy, int dz) {
        if (context.available && !context.available(x+dx,y+dy,z+dz)) return CollisionState {};
        if (context.stateAt) return context.stateAt(x+dx,y+dy,z+dz);
        if (!context.blockAt) return CollisionState {};
        return collisionState(context.blockAt(x+dx,y+dy,z+dz));
    };
    // A roof alone supports a hanging vine, matching the source survival rule
    // that only kills a vine with no facings once the block above stops being an
    // acceptable neighbour. Otherwise any of the four side faces will do: the
    // current catalog stores one representative vine state (data 15, all four
    // facings), so all four sides are candidates rather than data-selected.
    if (vineAttachable(neighbour(0,1,0).tileId)) return true;
    return vineAttachable(neighbour(-1,0,0).tileId) || vineAttachable(neighbour(1,0,0).tileId) ||
           vineAttachable(neighbour(0,0,-1).tileId) || vineAttachable(neighbour(0,0,1).tileId);
}
CollisionBoxes blockCollisionBoxes(CollisionState s, int x, int y, int z, const CollisionContext& context) {
    CollisionBoxes result;
    auto add = [&](float x0, float y0, float z0, float x1, float y1, float z1) {
        if (x0 < x1 && y0 < y1 && z0 < z1)
            result.boxes[size_t(result.count++)] = {x + x0, y + y0, z + z0, x + x1, y + y1, z + z1};
    };
    auto full = [&] { add(0,0,0,1,1,1); };
    auto read = [&](int dx, int dz, int dy = 0) {
        if (context.available && !context.available(x+dx,y+dy,z+dz)) return CollisionState {};
        if (context.stateAt) return context.stateAt(x+dx,y+dy,z+dz);
        if (!context.blockAt) return CollisionState {};
        return collisionState(context.blockAt(x+dx,y+dy,z+dz));
    };
    if (s.tileId == 0) return result;
    if (s.data < 0 || s.data > 15) { result.conservative = true; full(); return result; }
    switch (s.tileId) {
    // Bush-derived vegetation and audited null getAABB overrides. Web slowing,
    // liquid motion, fire/lava/cactus damage and redstone/ticks are not ported.
    case 8: case 9: case 10: case 11: case 30: case 31: case 32:
    case 37: case 38: case 39: case 40: case 50: case 51: case 55: case 59:
    case 66: case 69: case 70: case 72: case 77: case 83: case 106: case 115:
    case 131: case 132: case 141: case 142: return result;
    case 44: // HalfSlabTile::updateShape; double slab 43 is a full cube.
        if (s.data & 8) add(0,.5f,0,1,1,1); else add(0,0,0,1,.5f,1);
        return result;
    case 78: // Collision is data/8, NOT the renderer's (data+1)/8.
        add(0,0,0,1,float(s.data & 7)/8,1); return result;
    case 60: full(); return result; // FarmTile::getAABB overrides 15/16 render height.
    case 88: add(0,0,0,1,.875f,1); return result;
    case 81: add(.0625f,0,.0625f,.9375f,.9375f,.9375f); return result;
    case 54: {
        float x0=.0625f, z0=.0625f, x1=.9375f, z1=.9375f;
        if (read(0,-1).tileId == s.tileId) z0=0;
        else if (read(0,1).tileId == s.tileId) z1=1;
        else if (read(-1,0).tileId == s.tileId) x0=0;
        else if (read(1,0).tileId == s.tileId) x1=1;
        add(x0,0,z0,x1,.875f,z1); return result;
    }
    case 65: // LadderTile has a 2/16 backing collider, not a whole-cell wall.
        switch (s.data) {
        case 2: add(0,0,.875f,1,1,1); return result;
        case 3: add(0,0,0,1,1,.125f); return result;
        case 4: add(.875f,0,0,1,1,1); return result;
        case 5: add(0,0,0,.125f,1,1); return result;
        default: result.conservative=true; full(); return result;
        }
    case 64: case 71: {
        // Composite lower facing/open + upper hinge. Missing half is data0 as
        // in source Level::getData; valid upper halves track the lower state.
        const int lower = (s.data & 8) ? read(0,0,-1).data : s.data;
        const int upper = (s.data & 8) ? s.data : read(0,0,1).data;
        const int dir = lower & 3;
        const bool open = (lower & 4) != 0, right = (upper & 1) != 0;
        const float r=.1875f;
        if (!open) {
            if (dir==0) add(0,0,0,r,1,1);
            if (dir==1) add(0,0,0,1,1,r);
            if (dir==2) add(1-r,0,0,1,1,1);
            if (dir==3) add(0,0,1-r,1,1,1);
        } else {
            if (dir==0) { if (right) add(0,0,1-r,1,1,1); else add(0,0,0,1,1,r); }
            if (dir==1) { if (right) add(0,0,0,r,1,1); else add(1-r,0,0,1,1,1); }
            if (dir==2) { if (right) add(0,0,0,1,1,r); else add(0,0,1-r,1,1,1); }
            if (dir==3) { if (right) add(1-r,0,0,1,1,1); else add(0,0,0,r,1,1); }
        }
        return result;
    }
    case 85: case 113: {
        auto connects = [&](int dx,int dz) {
            const int tile = read(dx,dz).tileId;
            return tile==s.tileId || tile==107 || blockingCube(tile);
        };
        const bool n=connects(0,-1), south=connects(0,1), w=connects(-1,0), e=connects(1,0);
        if (n || south) add(.375f,0,n?0:.375f,.625f,1.5f,south?1:.625f);
        if (w || e || (!n && !south)) add(w?0:.375f,0,.375f,e?1:.625f,1.5f,.625f);
        return result;
    }
    case 139: {
        // WallTile: a 4/16 half-width post that always rises to y+1.5, with each
        // connected side stretching that arm out to the cell edge. The source
        // keeps the post width unchanged for the straight-through case (it was
        // deliberately left wider to stop entities popping up when a wall
        // segment gains a post), so this is a single box, not two.
        auto connects = [&](int dx,int dz) {
            const int tile = read(dx,dz).tileId;
            return tile==s.tileId || tile==107 || blockingCube(tile);
        };
        const bool n=connects(0,-1), south=connects(0,1), w=connects(-1,0), e=connects(1,0);
        add(w?0:.3125f, 0, n?0:.3125f, e?1:.6875f, 1.5f, south?1:.6875f);
        return result;
    }
    case 171:
        // WoolCarpetTile: a 1/16 thin sheet over the whole footprint. NOTE a
        // deliberate divergence: the tile declares a 1/16 shape (used for
        // rendering and picking), but its AABB accessor scales that height by a
        // hardcoded zero, so the original collider is degenerate and entities
        // walk flat across a carpet instead of stepping onto it. The 1/16 box
        // below is the specified behaviour for this port and is what the tests
        // pin; switch this to `return result;` to reproduce the no-collision
        // original. Either way a carpet is never tall enough to block a jump.
        add(0,0,0,1,.0625f,1); return result;
    case 116: // EnchantmentTableTile: 12/16 (3/4) tall, full horizontal cell.
        add(0,0,0,1,.75f,1); return result;
    case 145: {
        // AnvilTile: full-height cell narrowed by 2/16 on both sides of the
        // facing axis. data&3 is the facing; data>>2 is only damage, which does
        // not change the collider.
        const int dir = s.data & 3;
        if (dir==1 || dir==3) add(0,0,.125f,1,1,.875f); // facing west/east
        else add(.125f,0,0,.875f,1,1);                  // facing south/north
        return result;
    }
    case 101: case 102: case 160: {
        auto connects = [&](int dx,int dz) {
            const int tile=read(dx,dz).tileId;
            // ThinFence uses Tile::solid (render-solid registry), unlike Fence's
            // material/cube predicate: TNT/glowstone/pumpkin attach; spawners do not.
            // Panes additionally attach to glass and to either pane tile.
            return (blockingCube(tile) && tile!=52) || tile==46 || tile==86 || tile==89 ||
                   tile==s.tileId || tile==20 || tile==95 || tile==102 || tile==160;
        };
        const bool n=connects(0,-1), south=connects(0,1), w=connects(-1,0), e=connects(1,0);
        const bool alone=!n && !south && !w && !e;
        if ((w && e) || alone) add(0,0,.4375f,1,1,.5625f);
        else if (w) add(0,0,.4375f,.5f,1,.5625f);
        else if (e) add(.5f,0,.4375f,1,1,.5625f);
        if ((n && south) || alone) add(.4375f,0,0,.5625f,1,1);
        else if (n) add(.4375f,0,0,.5625f,1,.5f);
        else if (south) add(.4375f,0,.5f,.5625f,1,1);
        return result;
    }
    case 93: add(0,0,0,1,.125f,1); return result;
    case 111: add(0,0,0,1,.015625f,1); return result; // player, not boat
    case 118:
        add(0,0,0,1,.3125f,1); add(0,0,0,.125f,1,1);
        add(0,0,0,1,1,.125f); add(.875f,0,0,1,1,1); add(0,0,.875f,1,1,1);
        return result;
    case 120:
        add(0,0,0,1,.8125f,1);
        if (s.data & 4) add(.3125f,.8125f,.3125f,.6875f,1,.6875f);
        return result;
    case 127: {
        const int age=(s.data >> 2) & 3;
        const float width=float(4+age*2), height=float(5+age*2), half=width/2;
        const float lo=(8-half)/16, hi=(8+half)/16, bottom=(12-height)/16;
        switch (s.data & 3) {
        case 0: add(lo,bottom,(15-width)/16,hi,.75f,.9375f); break;
        case 1: add(.0625f,bottom,lo,(1+width)/16,.75f,hi); break;
        case 2: add(lo,bottom,.0625f,hi,.75f,(1+width)/16); break;
        case 3: add((15-width)/16,bottom,lo,.9375f,.75f,hi); break;
        }
        return result;
    }
    case 140: add(.3125f,0,.3125f,.6875f,.375f,.6875f); return result;
    default: break;
    }
    if (stairs(s.tileId)) {
        // StairTile::setBaseShape/setStepShape/setInnerPieceShape. Keep the
        // source neighbor-lock checks (including across different stair types).
        if (s.data > 7) { result.conservative=true; full(); return result; }
        const bool upper=(s.data & 4)!=0;
        const float bottom=upper?0:.5f, top=upper?.5f:1;
        add(0,upper?.5f:0,0,1,upper?1:.5f,1);
        const int dir=s.data & 3;
        const int dx[4]={1,-1,0,0}, dz[4]={0,0,1,-1};
        auto sameHalf=[&](CollisionState n) { return stairs(n.tileId) && ((n.data & 4)==(s.data & 4)); };
        auto lock=[&](int ox,int oz) { const auto n=read(ox,oz); return stairs(n.tileId) && n.data==s.data; };
        float west=0,east=1,north=0,south=.5f;
        if (dir==0) { west=.5f; south=1; }
        if (dir==1) { east=.5f; south=1; }
        if (dir==2) { north=.5f; south=1; }
        bool inner=true;
        const auto back=read(dx[dir],dz[dir]);
        if (sameHalf(back)) {
            const int other=back.data & 3;
            if (dir<2) {
                if (other==3 && !lock(0,1)) { south=.5f; inner=false; }
                else if (other==2 && !lock(0,-1)) { north=.5f; inner=false; }
            } else {
                if (other==1 && !lock(1,0)) { east=.5f; inner=false; }
                else if (other==0 && !lock(-1,0)) { west=.5f; inner=false; }
            }
        }
        add(west,bottom,north,east,top,south);
        const auto front=read(-dx[dir],-dz[dir]);
        if (inner && sameHalf(front)) {
            const int other=front.data & 3;
            west=0; east=.5f; north=.5f; south=1;
            bool piece=false;
            if (dir<2) {
                if (dir==1) { west=.5f; east=1; }
                if (other==3 && !lock(0,-1)) { north=0; south=.5f; piece=true; }
                else if (other==2 && !lock(0,1)) piece=true;
            } else {
                if (dir==2) { north=0; south=.5f; }
                if (other==1 && !lock(-1,0)) piece=true;
                else if (other==0 && !lock(1,0)) { west=.5f; east=1; piece=true; }
            }
            if (piece) add(west,bottom,north,east,top,south);
        }
        return result;
    }
    // Known current-catalog cubes (PistonBaseTile::addAABBs intentionally uses
    // full shape even when its rendered/selection base is retracted).
    // 95 stained glass and 122 dragon egg are known full-cell colliders that are
    // intentionally absent from blockingCube: neither attaches to fences/walls
    // (glass is never buildable; the egg is not cube-shaped), but both keep a
    // whole-cell AABB because they never override the default tile shape.
    if (!blockingCube(s.tileId) && s.tileId!=18 && s.tileId!=20 && s.tileId!=29 &&
        s.tileId!=46 && s.tileId!=79 && s.tileId!=86 && s.tileId!=89 &&
        s.tileId!=95 && s.tileId!=122)
        result.conservative=true;
    full(); return result;
}
} // namespace mcworld
