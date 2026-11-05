#include "ItemEntities.hpp"

#include "BlockCollision.hpp"
#include "Inventory.hpp"
#include "ItemCatalog.hpp"
#include "LegacyCatalog.hpp"
#include "ToolRules.hpp"
#include "World.hpp"

// Same original Random as Scene/terrain (Random.h needs the platform byte type).
#include <cstdint>
using byte = unsigned char;
#include <Random.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace mcscene {
namespace {
constexpr float Pi = 3.141592654f;
constexpr double HalfSize = ItemEntities::Size / 2;
constexpr int TileLava = 10, TileStillLava = 11, TileFire = 51, TileIce = 79;

struct Box {
    double x0, y0, z0, x1, y1, z1;
    Box grow(double x, double y, double z) const { return {x0 - x, y0 - y, z0 - z, x1 + x, y1 + y, z1 + z}; }
    Box move(double x, double y, double z) const { return {x0 + x, y0 + y, z0 + z, x1 + x, y1 + y, z1 + z}; }
    // AABB::expand: grows only towards the movement direction.
    Box expand(double x, double y, double z) const {
        Box r = *this;
        (x < 0 ? r.x0 : r.x1) += x; (y < 0 ? r.y0 : r.y1) += y; (z < 0 ? r.z0 : r.z1) += z;
        return r;
    }
    bool intersects(const Box& o) const {
        return o.x1 > x0 && o.x0 < x1 && o.y1 > y0 && o.y0 < y1 && o.z1 > z0 && o.z0 < z1;
    }
};
Box boxOf(const ItemEntity& e) {
    return {e.x - HalfSize, e.y, e.z - HalfSize, e.x + HalfSize, e.y + ItemEntities::Size, e.z + HalfSize};
}

int stackLimitOf(int itemId, int aux) {
    const int index = sourceitems::itemIndexForIdAux(itemId, aux);
    if (index < 0) return 0;
    const auto* tool = mcgame::toolProperties(itemId);
    return tool && tool->maxDamage > 0 ? 1 : sourceitems::kItems[index].maxStack;
}
int maxDamageOf(int itemId) {
    const auto* tool = mcgame::toolProperties(itemId);
    return tool ? tool->maxDamage : 0;
}

// Cell lookups. Below the world is void (items fall out and die at y < -64,
// Entity::baseTick outOfWorld); above it is air. Laterally outside the world
// or in an unloaded column is a full barrier, like World::solid.
struct Cells {
    const mcworld::World& world;
    bool available(int x, int y, int z) const {
        return y >= 0 && y < world.height() && world.chunkLoaded(x, z);
    }
    uint16_t block(int x, int y, int z) const {
        if (y < 0 || y >= world.height() || !world.chunkLoaded(x, z)) return 0;
        return world.blockAt(x, y, z);
    }
    int tile(int x, int y, int z) const {
        const uint16_t id = block(x, y, z);
        return id && id <= legacy::kBlockCount ? legacy::kBlockSpecs[id - 1].tileId : 0;
    }
    bool barrier(int x, int y, int z) const {
        if (y < 0 || y >= world.height()) return false;
        return x < 0 || z < 0 || x >= world.width() || z >= world.depth() || !world.chunkLoaded(x, z);
    }
    // Level::getCubes subset: every block collision box intersecting `area`.
    void cubes(const Box& area, std::vector<Box>& out) const {
        out.clear();
        const mcworld::CollisionContext context {
            [this](int x, int y, int z) { return block(x, y, z); },
            [this](int x, int y, int z) { return available(x, y, z); }};
        const int x0 = int(std::floor(area.x0)), x1 = int(std::floor(area.x1 + 1));
        const int y0 = int(std::floor(area.y0)) - 1, y1 = int(std::floor(area.y1 + 1));
        const int z0 = int(std::floor(area.z0)), z1 = int(std::floor(area.z1 + 1));
        for (int x = x0; x < x1; ++x) for (int z = z0; z < z1; ++z) for (int y = y0; y < y1; ++y) {
            if (barrier(x, y, z)) {
                const Box cell {double(x), double(y), double(z), double(x + 1), double(y + 1), double(z + 1)};
                if (cell.intersects(area)) out.push_back(cell);
                continue;
            }
            const uint16_t id = block(x, y, z);
            if (!id) continue;
            const auto boxes = mcworld::blockCollisionBoxes(mcworld::collisionState(id), x, y, z, context);
            for (int i = 0; i < boxes.count; ++i) {
                const auto& b = boxes.boxes[size_t(i)];
                const Box box {b.x0, b.y0, b.z0, b.x1, b.y1, b.z1};
                if (box.intersects(area)) out.push_back(box);
            }
        }
    }
    // Level::isFullAABBTile: a single unit collision box (or a barrier).
    bool fullCell(int x, int y, int z) const {
        if (barrier(x, y, z)) return true;
        const uint16_t id = block(x, y, z);
        if (!id) return false;
        const mcworld::CollisionContext context {
            [this](int a, int b, int c) { return block(a, b, c); },
            [this](int a, int b, int c) { return available(a, b, c); }};
        const auto boxes = mcworld::blockCollisionBoxes(mcworld::collisionState(id), x, y, z, context);
        if (boxes.count != 1) return false;
        const auto& b = boxes.boxes[0];
        return b.x0 <= x && b.y0 <= y && b.z0 <= z && b.x1 >= x + 1 && b.y1 >= y + 1 && b.z1 >= z + 1;
    }
    bool contains(const Box& box, int a, int b) const {
        const int x0 = int(std::floor(box.x0)), x1 = int(std::floor(box.x1 + 1));
        const int y0 = int(std::floor(box.y0)), y1 = int(std::floor(box.y1 + 1));
        const int z0 = int(std::floor(box.z0)), z1 = int(std::floor(box.z1 + 1));
        for (int x = x0; x < x1; ++x) for (int y = y0; y < y1; ++y) for (int z = z0; z < z1; ++z) {
            const int t = tile(x, y, z);
            if (t == a || t == b) return true;
        }
        return false;
    }
};

// AABB::clip{X,Y,Z}Collide (AABB.cpp): the static box `c` limits motion of `b`.
double clipY(const Box& c, const Box& b, double ya) {
    if (b.x1 <= c.x0 || b.x0 >= c.x1 || b.z1 <= c.z0 || b.z0 >= c.z1) return ya;
    if (ya > 0 && b.y1 <= c.y0) ya = std::min(ya, c.y0 - b.y1);
    if (ya < 0 && b.y0 >= c.y1) ya = std::max(ya, c.y1 - b.y0);
    return ya;
}
double clipX(const Box& c, const Box& b, double xa) {
    if (b.y1 <= c.y0 || b.y0 >= c.y1 || b.z1 <= c.z0 || b.z0 >= c.z1) return xa;
    if (xa > 0 && b.x1 <= c.x0) xa = std::min(xa, c.x0 - b.x1);
    if (xa < 0 && b.x0 >= c.x1) xa = std::max(xa, c.x1 - b.x0);
    return xa;
}
double clipZ(const Box& c, const Box& b, double za) {
    if (b.x1 <= c.x0 || b.x0 >= c.x1 || b.y1 <= c.y0 || b.y0 >= c.y1) return za;
    if (za > 0 && b.z1 <= c.z0) za = std::min(za, c.z0 - b.z1);
    if (za < 0 && b.z0 >= c.z1) za = std::max(za, c.z1 - b.z0);
    return za;
}

bool resident(const mcworld::World& world, const ItemEntity& e) {
    for (double dz : {-HalfSize, HalfSize}) for (double dx : {-HalfSize, HalfSize})
        if (!world.chunkLoaded(int(std::floor(e.x + dx)), int(std::floor(e.z + dz)))) return false;
    return true;
}

// Entity::checkInTile (Entity.cpp:1873-1934): push an item out of a block it
// is stuck in, towards the nearest open face (never down).
bool checkInTile(const Cells& cells, ItemEntity& e, Random& random, std::vector<Box>& scratch) {
    const double cx = e.x, cy = e.y + HalfSize, cz = e.z;
    const int xt = int(std::floor(cx)), yt = int(std::floor(cy)), zt = int(std::floor(cz));
    const double xd = cx - xt, yd = cy - yt, zd = cz - zt;
    cells.cubes(boxOf(e), scratch);
    if (scratch.empty() && !cells.fullCell(xt, yt, zt)) return false;
    const bool west = !cells.fullCell(xt - 1, yt, zt), east = !cells.fullCell(xt + 1, yt, zt);
    const bool up = !cells.fullCell(xt, yt + 1, zt);
    const bool north = !cells.fullCell(xt, yt, zt - 1), south = !cells.fullCell(xt, yt, zt + 1);
    int dir = 3; double closest = 9999;
    if (west && xd < closest) { closest = xd; dir = 0; }
    if (east && 1 - xd < closest) { closest = 1 - xd; dir = 1; }
    if (up && 1 - yd < closest) { closest = 1 - yd; dir = 3; }
    if (north && zd < closest) { closest = zd; dir = 4; }
    if (south && 1 - zd < closest) { closest = 1 - zd; dir = 5; }
    const float speed = random.nextFloat() * .2f + .1f;
    if (dir == 0) e.xd = -speed;
    if (dir == 1) e.xd = speed;
    if (dir == 3) e.yd = speed;
    if (dir == 4) e.zd = -speed;
    if (dir == 5) e.zd = speed;
    return true;
}

// Entity::move (Entity.cpp:701-890) with noEntityCubes, slide = true and
// footSize = 0 (items never step up). noPhysics moves straight through.
void move(const Cells& cells, ItemEntity& e, bool noPhysics, std::vector<Box>& cubes) {
    double xa = e.xd, ya = e.yd, za = e.zd;
    if (noPhysics) { e.x += xa; e.y += ya; e.z += za; return; }
    const double xaOrg = xa, yaOrg = ya, zaOrg = za;
    Box bb = boxOf(e);
    cells.cubes(bb.expand(xa, ya, za), cubes);
    for (const auto& c : cubes) ya = clipY(c, bb, ya);
    bb = bb.move(0, ya, 0);
    for (const auto& c : cubes) xa = clipX(c, bb, xa);
    bb = bb.move(xa, 0, 0);
    for (const auto& c : cubes) za = clipZ(c, bb, za);
    bb = bb.move(0, 0, za);
    e.x = (bb.x0 + bb.x1) / 2; e.y = bb.y0; e.z = (bb.z0 + bb.z1) / 2;
    e.onGround = yaOrg != ya && yaOrg < 0;
    if (xaOrg != xa) e.xd = 0;
    if (yaOrg != ya) e.yd = 0;
    if (zaOrg != za) e.zd = 0;
}

bool stackable(const ItemEntity& a, const ItemEntity& b) {
    // ItemEntity::merge (ItemEntity.cpp:146-167): same item, same aux (every
    // catalog identity is stacked by data), no tags; damaged stacks never merge.
    return a.itemId == b.itemId && a.aux == b.aux && a.damage == 0 && b.damage == 0;
}
} // namespace

bool parseItemKey(const char* key, int& itemId, int& aux) {
    if (!key || std::strncmp(key, "item:", 5)) return false;
    // Hand-rolled parsing on purpose: strtol pulls in __isoc23_strtol@GLIBC_2.38
    // on newer hosts, which the device's glibc 2.36 cannot resolve (Crafting.cpp).
    const auto digits = [](const char*& p, int& out) {
        const char* start = p;
        int value = 0;
        while (*p >= '0' && *p <= '9') {
            if (p - start >= 5) return false;
            value = value * 10 + (*p++ - '0');
        }
        out = value;
        return p != start && value <= 32767;
    };
    const char* p = key + 5;
    int id = 0, value = 0;
    if (!digits(p, id) || *p++ != ':' || !digits(p, value) || *p) return false;
    itemId = id; aux = value;
    return true;
}

bool ItemEntities::valid(const ItemEntity& e) {
    const int limit = stackLimitOf(e.itemId, e.aux);
    const auto isReal = [](double v) { return std::isfinite(v); };
    return limit > 0 && e.count >= 1 && e.count <= limit && e.damage >= 0 && e.damage <= maxDamageOf(e.itemId) &&
           isReal(e.x) && isReal(e.y) && isReal(e.z) && isReal(e.xd) && isReal(e.yd) && isReal(e.zd) &&
           std::abs(e.xd) <= 64 && std::abs(e.yd) <= 64 && std::abs(e.zd) <= 64 &&
           std::abs(e.x) < 1e7 && std::abs(e.z) < 1e7 && e.y >= -64 && e.y < 1e6 &&
           e.age >= 0 && e.age < Lifetime && e.pickupDelay >= 0 && e.pickupDelay <= 32767 &&
           e.health > 0 && e.health <= 255 && std::isfinite(e.bobOffs) && e.bobOffs >= 0 && e.bobOffs < 2 * Pi + 1e-3f;
}

int64_t ItemEntities::nextSeed() {
    // Deterministic per event: world seed and a monotonically increasing serial.
    const uint64_t bits = uint64_t(seed_) * UINT64_C(0x9E3779B97F4A7C15) + (++serial_) * UINT64_C(341873128712);
    int64_t result; std::memcpy(&result, &bits, 8); return result;
}

bool ItemEntities::add(ItemEntity item) {
    item.stackLimit = stackLimitOf(item.itemId, item.aux);
    if (!valid(item)) return false;
    // ServerLevel::addEntity: at capacity the OLDEST item entity goes first.
    while (items_.size() >= size_t(Capacity)) items_.erase(items_.begin());
    items_.push_back(item);
    return true;
}

bool ItemEntities::popResource(int x, int y, int z, int itemId, int aux, int count, int damage) {
    Random random(nextSeed());
    const float s = .7f;
    ItemEntity e;
    e.itemId = itemId; e.aux = aux; e.count = count; e.damage = damage;
    e.x = x + random.nextFloat() * s + (1 - s) * .5;
    const double yo = random.nextFloat() * s + (1 - s) * .5;
    e.z = z + random.nextFloat() * s + (1 - s) * .5;
    e.y = y + yo - HalfSize; // setPos(x, y, z) centres the box on y (heightOffset .125).
    // ItemEntity(level, x, y, z) constructor (ItemEntity.cpp:23, 41-43).
    e.bobOffs = float(random.nextFloat() * Pi * 2);
    e.xd = random.nextFloat() * .2f - .1f; e.yd = .2f; e.zd = random.nextFloat() * .2f - .1f;
    e.pickupDelay = BlockDropDelay;
    return add(e);
}

void ItemEntities::dropContents(int x, int y, int z, const std::vector<mcinventory::Slot>& slots) {
    Random random(nextSeed());
    for (const auto& slot : slots) {
        int itemId = 0, aux = 0;
        if (slot.count <= 0 || !parseItemKey(slot.key.c_str(), itemId, aux)) continue;
        const float xo = random.nextFloat() * .8f + .1f, yo = random.nextFloat() * .8f + .1f;
        const float zo = random.nextFloat() * .8f + .1f;
        for (int left = slot.count; left > 0;) {
            int count = random.nextInt(21) + 10;
            if (count > left) count = left;
            left -= count;
            ItemEntity e;
            e.itemId = itemId; e.aux = aux; e.count = count; e.damage = slot.damage;
            e.x = x + xo; e.y = y + yo - HalfSize; e.z = z + zo;
            e.bobOffs = float(random.nextFloat() * Pi * 2);
            const float pow = .05f;
            e.xd = float(random.nextGaussian()) * pow;
            e.yd = float(random.nextGaussian()) * pow + .2f;
            e.zd = float(random.nextGaussian()) * pow;
            add(e);
        }
    }
}

bool ItemEntities::dropFromPlayer(float feetX, float feetY, float feetZ, const mcinventory::Slot& slot) {
    int itemId = 0, aux = 0;
    if (slot.count <= 0 || !parseItemKey(slot.key.c_str(), itemId, aux)) return false;
    Random random(nextSeed());
    ItemEntity e;
    e.itemId = itemId; e.aux = aux; e.count = slot.count; e.damage = slot.damage;
    // Player.cpp:1173: (x, y - .3 + getHeadHeight(), z); the player's y is feet
    // + heightOffset 1.62 and Player::getHeadHeight() is .12.
    e.x = feetX; e.z = feetZ; e.y = feetY + 1.62 - .3 + .12 - HalfSize;
    e.bobOffs = float(random.nextFloat() * Pi * 2);
    e.pickupDelay = PlayerDropDelay;
    const float power = random.nextFloat() * .5f, dir = random.nextFloat() * Pi * 2;
    e.xd = -std::sin(dir) * power; e.zd = std::cos(dir) * power; e.yd = .2f;
    return add(e);
}

void ItemEntities::merge(size_t index) {
    // ItemEntity::mergeWithNeighbours: bb grown by (.5, 0, .5).
    ItemEntity& self = items_[index];
    const Box area = boxOf(self).grow(.5, 0, .5);
    for (size_t j = 0; j < items_.size() && self.count > 0; ++j) {
        if (j == index) continue;
        ItemEntity& other = items_[j];
        if (other.count <= 0 || !boxOf(other).intersects(area) || !stackable(self, other)) continue;
        // The larger stack absorbs the smaller (ItemEntity.cpp:157-164).
        ItemEntity& into = other.count < self.count ? self : other;
        ItemEntity& from = other.count < self.count ? other : self;
        if (into.count + from.count > into.stackLimit) continue;
        into.count += from.count;
        into.pickupDelay = std::max(into.pickupDelay, from.pickupDelay);
        into.age = std::min(into.age, from.age);
        from.count = 0;
    }
}

void ItemEntities::tick(const mcworld::World& world) {
    if (items_.empty()) return;
    const Cells cells {world};
    thread_local std::vector<Box> scratch;
    Random random(nextSeed());
    for (size_t i = 0; i < items_.size(); ++i) {
        ItemEntity& e = items_[i];
        if (e.count <= 0 || !resident(world, e)) continue;
        // Entity::baseTick: lava hurts 4 (Entity.cpp:650-660), void kills.
        Box bb = boxOf(e);
        if (cells.contains(bb.grow(-.1, -.4, -.1), TileLava, TileStillLava)) e.health -= 4;
        if (e.y < -64) e.health = 0;
        const int oldX = int(e.x), oldY = int(e.y + HalfSize), oldZ = int(e.z); // (int) truncation, as source
        // ItemEntity::tick (ItemEntity.cpp:72-133).
        if (e.pickupDelay > 0) --e.pickupDelay;
        e.yd -= .04f;
        const bool noPhysics = checkInTile(cells, e, random, scratch);
        move(cells, e, noPhysics, scratch);
        // Entity::move fire contact (Entity.cpp:941-944): burn(1).
        if (cells.contains(boxOf(e).grow(-.001, -.001, -.001), TileFire, TileFire)) e.health -= 1;
        const bool moved = oldX != int(e.x) || oldY != int(e.y + HalfSize) || oldZ != int(e.z);
        if (moved || e.age % 25 == 0) {
            const int fx = int(std::floor(e.x)), fy = int(std::floor(e.y + HalfSize)), fz = int(std::floor(e.z));
            const int here = cells.tile(fx, fy, fz);
            if (here == TileLava || here == TileStillLava) {
                e.yd = .2f;
                e.xd = (random.nextFloat() - random.nextFloat()) * .2f;
                e.zd = (random.nextFloat() - random.nextFloat()) * .2f;
            }
            merge(i);
            if (e.count <= 0) continue;
        }
        float friction = .98f;
        if (e.onGround) {
            friction = .6f * .98f;
            const int below = cells.tile(int(std::floor(e.x)), int(std::floor(e.y)) - 1, int(std::floor(e.z)));
            if (below > 0) friction = (below == TileIce ? .98f : .6f) * .98f; // Tile::friction; IceTile.cpp:11
        }
        e.xd *= friction; e.yd *= .98f; e.zd *= friction;
        if (e.onGround) e.yd *= -.5f;
        ++e.age;
        if (e.age >= Lifetime || e.health <= 0) e.count = 0;
    }
    items_.erase(std::remove_if(items_.begin(), items_.end(), [](const ItemEntity& e) { return e.count <= 0; }),
                 items_.end());
}

namespace {
// Largest n <= count that Inventory::canAddItem accepts (monotonic in n).
int fitting(const mcinventory::Inventory& inventory, const ItemEntity& e) {
    if (inventory.canAddItem(e.itemId, e.aux, e.count, e.damage)) return e.count;
    // Damaged items need one free slot for the whole stack (Inventory.cpp:397).
    if (e.damage) return 0;
    int low = 0, high = e.count - 1;
    while (low < high) {
        const int mid = (low + high + 1) / 2;
        if (inventory.canAddItem(e.itemId, e.aux, mid, e.damage)) low = mid; else high = mid - 1;
    }
    return low;
}
} // namespace

int ItemEntities::pickup(const mcworld::World& world, mcinventory::Inventory& inventory, bool alive) {
    // Player::aiStep only touches entities while getHealth() > 0.
    if (!alive || items_.empty() || !inventory.opened()) return 0;
    constexpr double Radius = .3; // Player setSize(.6, 1.8): bb half-width .3.
    const double px = world.playerX(), py = world.playerY(), pz = world.playerZ();
    const Box area = Box {px - Radius, py, pz - Radius, px + Radius, py + 1.8, pz + Radius}.grow(1, .5, 1);
    const bool creative = inventory.gameMode() == mcinventory::GameMode::Creative;
    int taken = 0;
    for (auto& e : items_) {
        if (e.count <= 0 || e.pickupDelay > 0 || !boxOf(e).intersects(area)) continue;
        const int n = fitting(inventory, e);
        if (n > 0 && inventory.addItem(e.itemId, e.aux, n, e.damage)) { e.count -= n; taken += n; }
        // Creative instabuild silently destroys whatever did not fit.
        if (creative) e.count = 0;
    }
    items_.erase(std::remove_if(items_.begin(), items_.end(), [](const ItemEntity& e) { return e.count <= 0; }),
                 items_.end());
    return taken;
}

bool ItemEntities::replace(std::vector<ItemEntity> items, uint64_t serial) {
    if (items.size() > size_t(Capacity)) return false;
    for (auto& e : items) {
        e.stackLimit = stackLimitOf(e.itemId, e.aux);
        if (!valid(e)) return false;
    }
    items_ = std::move(items);
    serial_ = serial;
    return true;
}
} // namespace mcscene
