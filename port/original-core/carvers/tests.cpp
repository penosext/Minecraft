// Standalone carver/ore module tests against a synthetic stone column world.
// Real terrain/biome integration is covered by tests/carvers-world-test.cpp.
#include "Carvers.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <thread>

using namespace original_carvers;
namespace {
// Bedrock y=0, stone 1..59, dirt 60..62, grass 63, air above; x-z-y layout.
std::vector<uint8_t> flatChunk() {
    std::vector<uint8_t> t(16 * 128 * 16, 0);
    for (int x = 0; x < 16; ++x) for (int z = 0; z < 16; ++z) for (int y = 0; y < 64; ++y)
        t[(x * 16 + z) * 128 + y] = y == 0 ? 7 : y < 60 ? 1 : y < 63 ? 3 : 2;
    return t;
}
std::vector<uint8_t> carved(int64_t seed, int cx, int cz) {
    auto tiles = flatChunk();
    std::vector<uint8_t> top(256, 2);
    Carver(seed).apply(cx, cz, tiles.data(), top.data());
    return tiles;
}
uint64_t fnv(const std::vector<uint8_t>& v) {
    uint64_t h = 1469598103934665603ull;
    for (uint8_t b : v) { h ^= b; h *= 1099511628211ull; }
    return h;
}
bool flatCells(int, int, uint8_t* cells) {
    for (int y = 0; y < 64; ++y) for (int i = 0; i < 256; ++i)
        cells[y * 256 + i] = y == 0 ? 7 : y < 60 ? 1 : y < 63 ? 3 : 2;
    return true;
}
void caves() {
    // Over many chunks, a fraction must be carved; bedrock stays; lava only <10.
    int carvedChunks = 0, lava = 0, air = 0;
    for (int cx = -8; cx < 8; ++cx) for (int cz = -8; cz < 8; ++cz) {
        const auto base = flatChunk(), t = carved(12345, cx, cz);
        bool changed = false;
        for (int x = 0; x < 16; ++x) for (int z = 0; z < 16; ++z) for (int y = 0; y < 128; ++y) {
            const size_t at = (x * 16 + z) * 128 + y;
            if (y == 0) assert(t[at] == 7);
            if (t[at] == base[at]) continue;
            changed = true;
            assert(t[at] == 0 || t[at] == 10 || (t[at] == 2 && base[at] == 3));
            // Original p starts at y1 while yy=y1-1: writes land one cell above yy.
            if (t[at] == 10) { assert(y <= 10); ++lava; }
            if (t[at] == 0) { assert(y >= 11 && y <= 120); ++air; }
        }
        carvedChunks += changed;
    }
    std::cout << "carved chunks=" << carvedChunks << "/256 air=" << air << " lava=" << lava << '\n';
    assert(carvedChunks > 16 && carvedChunks < 256 && air > 1000);
}
void determinism() {
    // Same chunk regardless of the order and of a reused Carver instance.
    Carver reused(777);
    std::map<std::pair<int,int>, uint64_t> first;
    for (int cx = -3; cx <= 3; ++cx) for (int cz = -3; cz <= 3; ++cz) first[{cx, cz}] = fnv(carved(777, cx, cz));
    std::vector<uint8_t> top(256, 2);
    for (int cx = 3; cx >= -3; --cx) for (int cz = 3; cz >= -3; --cz) {
        auto t = flatChunk(); reused.apply(cx, cz, t.data(), top.data());
        assert((fnv(t) == first[{cx, cz}]));
    }
    bool differs = false;
    for (int cx = -3; cx <= 3; ++cx) differs |= fnv(carved(778, cx, 0)) != first[std::make_pair(cx, 0)];
    assert(differs);
}
void ores() {
    std::map<int, int> perTile, minY, maxY;
    for (int xt = -4; xt < 4; ++xt) for (int zt = -4; zt < 4; ++zt) {
        const auto a = planOres(12345, xt, zt, flatCells), b = planOres(12345, xt, zt, flatCells);
        assert(a.size() == b.size() && !a.empty());
        for (size_t i = 0; i < a.size(); ++i) {
            assert(!std::memcmp(&a[i], &b[i], sizeof(Block)));
            const auto& o = a[i];
            // Writes only reach origin chunk and its +1 neighbors; stone only.
            assert(o.x >= xt * 16 && o.x < xt * 16 + 32 && o.z >= zt * 16 && o.z < zt * 16 + 32);
            assert(o.y >= 1 && o.y < 60);
            ++perTile[o.tile];
            minY.emplace(o.tile, o.y); maxY.emplace(o.tile, o.y);
            minY[o.tile] = std::min(minY[o.tile], o.y); maxY[o.tile] = std::max(maxY[o.tile], o.y);
        }
    }
    for (auto& e : perTile) std::cout << "ore tile=" << e.first << " cells=" << e.second
                                      << " y=" << minY[e.first] << ".." << maxY[e.first] << '\n';
    for (int t : {3, 13, 16, 15, 14, 73, 56, 21}) assert(perTile[t] > 0);
    // Original depth bands (decorateDepthSpan y1, +vein radius slack).
    assert(maxY[15] < 64 + 4 && maxY[14] < 32 + 4 && maxY[73] < 16 + 4 && maxY[56] < 16 + 4 && maxY[21] < 32 + 4);
    assert(perTile[16] > perTile[56] && perTile[15] > perTile[14]);
    // Outside-world chunks provide no stone.
    assert(planOres(1, 0, 0, [](int, int, uint8_t*) { return false; }).empty());
}
void threaded() {
    const auto expected = fnv(carved(4242, 2, -5));
    const auto ore = planOres(4242, 2, -5, flatCells).size();
    std::vector<std::thread> threads;
    bool ok[4] = {};
    for (int i = 0; i < 4; ++i) threads.emplace_back([&, i] {
        ok[i] = fnv(carved(4242, 2, -5)) == expected && planOres(4242, 2, -5, flatCells).size() == ore;
    });
    for (auto& t : threads) t.join();
    for (bool b : ok) assert(b);
}
}
int main(int argc, char** argv) {
    const std::string c = argc > 1 ? argv[1] : "";
    if (c == "caves" || c.empty()) caves();
    if (c == "determinism" || c.empty()) determinism();
    if (c == "ores" || c.empty()) ores();
    if (c == "threaded" || c.empty()) threaded();
    std::cout << "carvers " << (c.empty() ? "all" : c) << " passed\n";
}
