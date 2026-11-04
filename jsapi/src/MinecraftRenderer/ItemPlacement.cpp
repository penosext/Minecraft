#include "ItemPlacement.hpp"
#include "BlockCollision.hpp"
#include "ItemCatalog.hpp"
#include "LegacyCatalog.hpp"

#include <cmath>
#include <cstring>

namespace mcworld {
const PlacementSpec* placementItem(int sourceItemId, int aux) {
    for (int i = 0; i < sourceitems::itemCount(); ++i) {
        const auto& item = sourceitems::items()[i];
        if (item.itemId == sourceItemId && item.aux == aux) return &item;
    }
    return nullptr;
}
int placementState(int tileId, int data) {
    if (data < 0 || data > 15) return -1;
    int result = -1;
    for (int i = 0; i < sourceitems::blockStateCount(); ++i) {
        const auto& state = sourceitems::blockStates()[i];
        if (state.tileId != tileId || state.data != data || state.renderIndex < 0 || state.renderIndex >= legacy::kBlockCount) continue;
        if (result < 0) result = state.renderIndex;
        // Prefer explicit state keys over older representative duplicates. Other
        // duplicates (snowy grass, opaque leaves) keep the first canonical key.
        if (std::strstr(state.renderCatalogKey, "_data_")) return state.renderIndex;
    }
    if (result >= 0 || (tileId != 106 && tileId != 127)) return result;
    // Vine/cocoa facing states are static generate-terrain additions, absent
    // from ItemCatalog's pinned 312 rows; their render spec carries the exact
    // source tileId/data. Limited to these tiles: lever/button/repeater powered
    // states are deliberately NOT reachable through placement/interaction.
    for (int i = 0; i < legacy::kBlockCount; ++i)
        if (legacy::kBlockSpecs[i].tileId == tileId && legacy::kBlockSpecs[i].data == data) return i;
    return -1;
}
namespace {
struct State { int tile = 0, data = 0; };
State stateOf(uint16_t block) {
    const auto state = collisionState(block); // full render catalog, O(1)
    return {state.tileId, state.data};
}
bool stairs(int tile) {
    switch (tile) {
    case 53: case 67: case 109: case 114: case 128: case 134: return true;
    default: return false;
    }
}
// Audited catalog subset of Tile::isSolidBlockingTile. Do not confuse World's
// conservative full-cell collision with upstream material/cube support rules.
bool cubeSupport(int tile) {
    switch (tile) {
    case 1: case 2: case 3: case 4: case 5: case 7: case 12: case 13:
    case 14: case 15: case 16: case 17: case 21: case 23: case 24: case 35: case 43:
    case 47: case 48: case 49: case 52: case 56: case 58: case 61: case 73: case 74:
    case 80: case 82: case 86: case 87: case 88: case 97: case 98: case 99: case 100:
    case 110: case 112: case 121: case 129: case 153: return true;
    default: return false;
    }
}
bool knownSupport(int tile) {
    if (cubeSupport(tile) || stairs(tile)) return true;
    switch (tile) {
    case 0: case 8: case 9: case 10: case 11: case 18: case 20: case 29: case 30:
    case 31: case 32: case 37: case 38: case 39: case 40: case 44: case 46: case 50:
    case 51: case 54: case 55: case 59: case 60: case 64: case 65: case 66: case 69:
    case 70: case 71: case 72: case 77: case 78: case 79: case 81: case 83: case 85:
    case 89: case 93: case 101: case 102: case 106: case 111: case 113: case 115:
    case 118: case 120: case 127: case 131: case 132: case 140: case 141: case 142: return true;
    default: return false;
    }
}
bool ordinaryMaterial(int tile) {
    // Tile registrations with inherited TileItem placement and no placement
    // orientation/custom-use hook. Never infer this property from render shape.
    switch (tile) {
    case 1: case 2: case 3: case 4: case 5: case 7: case 12: case 13:
    case 14: case 15: case 16: case 18: case 20: case 21: case 24: case 35:
    case 46: case 47: case 48: case 49: case 56: case 58: case 73: case 79:
    case 80: case 82: case 87: case 88: case 89: case 98: case 110: case 112:
    case 121: case 129: case 153:
    // Plain TileItem/MultiTextureTileItem without a placement hook: web,
    // monster egg (aux = stone kind) and the neighbour-connected fences/panes,
    // whose connections are derived from neighbours, never from metadata.
    case 30: case 85: case 97: case 101: case 102: case 113: return true;
    default: return false;
    }
}
bool fence(int tile) { return tile == 85 || tile == 113; }
// Material::isSolid() is false only for gas/liquid/decoration/portal materials.
// Used by CactusTile::canSurvive on its four horizontal neighbours.
bool nonSolidMaterial(int tile) {
    switch (tile) {
    case 0: case 8: case 9: case 10: case 11: case 6: case 31: case 32: case 37:
    case 38: case 39: case 40: case 50: case 51: case 55: case 59: case 63: case 65:
    case 66: case 68: case 69: case 75: case 76: case 77: case 78: case 83: case 90:
    case 93: case 94: case 104: case 105: case 106: case 111: case 115: case 119:
    case 127: case 131: case 132: case 140: case 141: case 142: case 143: case 171: return true;
    default: return false;
    }
}
bool replaceable(State state) {
    return state.tile == 0 || state.tile == 8 || state.tile == 9 || state.tile == 10 || state.tile == 11 ||
           state.tile == 31 || state.tile == 32 || state.tile == 106 || (state.tile == 78 && state.data == 0);
}
}
bool planItemPlacement(const PlacementSpec& item, const PlacementContext& c, std::vector<PlacementChange>& changes) {
    changes.clear();
    if (!item.placeable || item.tileId <= 0 || !c.hit.hit || c.hit.face < 0 || c.hit.face > 5 ||
        !std::isfinite(c.yaw) || !std::isfinite(c.clickHeight) || c.clickHeight < 0 || c.clickHeight > 1 || !c.blockAt || !c.available) return false;
    std::vector<PlacementChange> planned;
    const auto read = [&](int x, int y, int z) { return stateOf(c.blockAt(x, y, z)); };
    const auto blocking = [&](int x, int y, int z) { return c.available(x,y,z) && cubeSupport(read(x,y,z).tile); };
    const auto top = [&](int x, int y, int z) {
        if (!c.available(x,y,z)) return false;
        const auto state = read(x,y,z);
        return cubeSupport(state.tile) ||
               (stairs(state.tile) && (state.data & 4)) || (state.tile == 44 && (state.data & 8)) ||
               (state.tile == 78 && state.data == 7);
    };
    const auto empty = [&](int x, int y, int z) { return c.available(x,y,z) && read(x,y,z).tile == 0; };
    const auto emit = [&](int x, int y, int z, int tile, int data) {
        if (!c.available(x,y,z)) return false;
        const int render = placementState(tile, data);
        if (render < 0) return false;
        planned.push_back({x,y,z,render}); return true;
    };
    static constexpr int offset[6][3] = {{0,-1,0},{0,1,0},{0,0,-1},{0,0,1},{-1,0,0},{1,0,0}};
    int face = c.hit.face;
    int x = c.hit.x, y = c.hit.y, z = c.hit.z;
    const State hit = read(x,y,z);
    const int tile = item.tileId;
    int data = item.defaultData;
    // World yaw increases toward +X; upstream yRot increases toward -X.
    const double sourceYaw = -std::remainder(double(c.yaw), 360.0);
    const int dir = int(std::floor(sourceYaw / 90.0 + .5)) & 3;
    if (tile == 44) {
        // StoneSlabTileItem::useOn, then tryConvertTargetTile, then TileItem.
        if (hit.tile == tile && (hit.data & 7) == item.aux &&
            ((face == 1 && !(hit.data & 8)) || (face == 0 && (hit.data & 8)))) {
            if (!emit(x,y,z,43,item.aux)) return false;
            changes.swap(planned); return true;
        }
        const auto* d = offset[face];
        const State target = read(x+d[0],y+d[1],z+d[2]);
        if (target.tile == tile && (target.data & 7) == item.aux) {
            if (!emit(x+d[0],y+d[1],z+d[2],43,item.aux)) return false;
            changes.swap(planned); return true;
        }
    }
    if (tile == 78 && hit.tile == 78 && hit.data <= 6) {
        if (!emit(x,y,z,tile,hit.data+1)) return false;
        changes.swap(planned); return true;
    }
    if (tile == 111) {
        // WaterLilyTileItem::use uses a liquid-aware ray, not TileItem::useOn.
        // Only known source water (calmWater/data0) can be represented exactly.
        if (hit.tile != 9 || hit.data != 0 || !empty(x,y+1,z) || !emit(x,y+1,z,tile,0)) return false;
        changes.swap(planned); return true;
    }
    if (tile == 59 || tile == 141 || tile == 142 || tile == 115) {
        // SeedItem/SeedFoodItem: exact source ItemSpec result tile; wheat seeds
        // are 295, not the unrelated wheat Item 296. No invented ID alias.
        // Nether wart seeds (372) are a SeedItem whose targetLand is soul sand.
        const int soil = tile == 115 ? 88 : 60;
        if (face != 1 || hit.tile != soil || !empty(x,y+1,z) || !emit(x,y+1,z,tile,0)) return false;
        changes.swap(planned); return true;
    }
    if (tile == 127) {
        // DyePowderItem::useOn BROWN: clicked block must be a jungle log, side
        // faces only, target cell empty. CocoaTile::getPlacedOnFaceDataValue =
        // DIRECTION_OPPOSITE[FACING_DIRECTION[face]], i.e. Direction toward
        // the log (S=0,W=1,N=2,E=3). Age 0.
        static constexpr int cocoa[6] = {-1,-1,0,2,3,1};
        if (hit.tile != 17 || (hit.data & 3) != 3 || face < 2) return false;
        const auto* d = offset[face];
        if (!empty(x+d[0],y+d[1],z+d[2]) || !emit(x+d[0],y+d[1],z+d[2],tile,cocoa[face])) return false;
        changes.swap(planned); return true;
    }
    if (tile == 64 || tile == 71) {
        // DoorItem::useOn / place and DoorTile::mayPlace. The upper hinge bit is
        // independent of lower direction; never copy direction into upper data.
        if (face != 1) return false;
        ++y;
        if (!top(x,y-1,z) || !c.available(x,y,z) || !c.available(x,y+1,z) ||
            !replaceable(read(x,y,z)) || !replaceable(read(x,y+1,z))) return false;
        const int doorDir = int(std::floor((sourceYaw + 180.0) / 90.0 - .5)) & 3;
        const int dx[] = {0,-1,0,1}, dz[] = {1,0,-1,0};
        const int lx=x-dx[doorDir], lz=z-dz[doorDir], rx=x+dx[doorDir], rz=z+dz[doorDir];
        for (int cy : {y,y+1}) {
            if (!c.available(lx,cy,lz) || !c.available(rx,cy,rz) ||
                !knownSupport(read(lx,cy,lz).tile) || !knownSupport(read(rx,cy,rz).tile)) return false;
        }
        const int left = int(blocking(lx,y,lz)) + int(blocking(lx,y+1,lz));
        const int right = int(blocking(rx,y,rz)) + int(blocking(rx,y+1,rz));
        const bool doorLeft = read(lx,y,lz).tile == tile || read(lx,y+1,lz).tile == tile;
        const bool doorRight = read(rx,y,rz).tile == tile || read(rx,y+1,rz).tile == tile;
        const bool flip = (doorLeft && !doorRight) || right > left;
        if (!emit(x,y,z,tile,doorDir) || !emit(x,y+1,z,tile,8 | int(flip))) return false;
        changes.swap(planned); return true;
    }
    // TileItem::useOn replaces thin snow/vegetation at the hit cell.
    if (hit.tile == 78 && hit.data == 0) face = 1;
    else if (hit.tile != 31 && hit.tile != 32 && hit.tile != 106) {
        x += offset[face][0]; y += offset[face][1]; z += offset[face][2];
    }
    if (!c.available(x,y,z) || !replaceable(read(x,y,z))) return false;
    if (stairs(tile)) {
        static constexpr int direction[] = {2,1,3,0};
        data = direction[dir] | ((face == 0 || (face != 1 && c.clickHeight > .5f)) ? 4 : 0);
    } else if (tile == 44) {
        data = item.aux | ((face == 0 || (face != 1 && c.clickHeight > .5f)) ? 8 : 0);
    } else if (tile == 17) {
        data = (item.aux & 3) | (face >= 4 ? 4 : face >= 2 ? 8 : 0);
    } else if (tile == 65 || tile == 50 || tile == 77) {
        if (tile == 50 && face == 1) {
            const int below = read(x,y-1,z).tile;
            if (!top(x,y-1,z) && !fence(below) && below != 20) return false;
            data = 5;
        } else {
            // This subset requires the clicked attachment face itself to be
            // valid; it never silently mounts to an unrelated neighboring face.
            if (face < 2 || !blocking(x-offset[face][0],y,z-offset[face][2])) return false;
            static constexpr int attachment[] = {0,0,4,3,2,1};
            data = tile == 65 ? face : attachment[face];
        }
    } else if (tile == 54 || tile == 61 || tile == 23) {
        static constexpr int facing[] = {2,5,3,4};
        data = facing[dir];
        if (tile == 54) {
            // Double chest neighbor rewrites/entity inventory are outside this
            // subset. Refuse rather than produce an inconsistent default state.
            for (int f=2; f<6; ++f) {
                const int nx=x+offset[f][0], nz=z+offset[f][2];
                if (!c.available(nx,y,nz) || read(nx,y,nz).tile == tile) return false;
            }
        }
        if (tile == 23 && std::abs(c.playerX-x)<2 && std::abs(c.playerZ-z)<2) {
            const double py = double(c.playerY) + 1.82; // source feet + 1.82
            if (py-y > 2) data=1;
            else if (y-py > 0) data=0;
            // Vertical dispenser states absent from current catalog -> false.
        }
    } else if (tile == 70 || tile == 72) {
        if (!top(x,y-1,z) && !fence(read(x,y-1,z).tile)) return false;
        data=0;
    } else if (tile == 78) {
        const auto below=read(x,y-1,z);
        if (!blocking(x,y-1,z) && below.tile != 18 && !(below.tile == 78 && below.data == 7)) return false;
        data=item.defaultData;
    } else if (tile == 31 || tile == 32 || tile == 37 || tile == 38 || tile == 39 || tile == 40) {
        // Bush::mayPlaceOn grass/dirt/farmland (tall grass, flowers);
        // DeadBushTile sand; Mushroom::canSurvive mycelium or Tile::solid.
        // Mushroom's daytime-brightness<13 term needs light the planner does
        // not have, so it is not enforced here.
        if (!c.available(x,y-1,z)) return false;
        const int below=read(x,y-1,z).tile;
        if (tile == 32 ? below != 12 :
            tile >= 39 ? below != 110 && !cubeSupport(below) :
                         below != 2 && below != 3 && below != 60) return false;
        data=item.defaultData;
    } else if (tile == 81) {
        // CactusTile::canSurvive: no solid material on the 4 sides; sand/cactus below.
        for (int f=2; f<6; ++f) {
            const int nx=x+offset[f][0], nz=z+offset[f][2];
            if (!c.available(nx,y,nz) || !nonSolidMaterial(read(nx,y,nz).tile)) return false;
        }
        if (!c.available(x,y-1,z)) return false;
        const int below=read(x,y-1,z).tile;
        if (below != 81 && below != 12) return false;
        data=0;
    } else if (tile == 83) {
        // TilePlanterItem(reeds) -> ReedTile::mayPlace: reed below, or
        // grass/dirt/sand below with water beside that block.
        if (!c.available(x,y-1,z)) return false;
        const int below=read(x,y-1,z).tile;
        if (below != 83) {
            if (below != 2 && below != 3 && below != 12) return false;
            bool water=false;
            for (int f=2; f<6; ++f) {
                const int nx=x+offset[f][0], nz=z+offset[f][2];
                if (!c.available(nx,y-1,nz)) return false;
                const int t=read(nx,y-1,nz).tile;
                water = water || t == 8 || t == 9;
            }
            if (!water) return false;
        }
        data=0;
    } else if (tile == 140) {
        // TilePlanterItem(flowerPot) -> FlowerPotTile::mayPlace: top-solid below.
        if (!top(x,y-1,z)) return false;
        data=0;
    } else if (tile == 106) {
        // VineTile::mayPlace(face) + getPlacedOnFaceDataValue. Only side faces;
        // the clicked block (cell behind the vine) must be cube-shaped and
        // motion-blocking. N->VINE_SOUTH(1), S->NORTH(4), W->EAST(8), E->WEST(2).
        static constexpr int vine[6] = {0,0,1,4,8,2};
        if (face < 2) return false;
        const int sx=x-offset[face][0], sz=z-offset[face][2];
        if (!c.available(sx,y,sz) || !collisionVineAttachable(read(sx,y,sz).tile)) return false;
        data=vine[face];
    } else {
        // Ordinary source TileItem states only. All other stateful/custom-use
        // shapes (repeater, rails, trapdoors, plants etc.) explicitly unsupported.
        if (item.itemId != tile || !ordinaryMaterial(tile)) return false;
    }
    // Leaf persistence/decay flags are not render variants; retain species,
    // exactly as OriginalWorld's established catalogForTile mapping does.
    if (tile == 18) data &= 3;
    if (!emit(x,y,z,tile,data)) return false;
    changes.swap(planned); return true;
}
} // namespace mcworld
