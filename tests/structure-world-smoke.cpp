#include "OriginalWorld.hpp"
#include "WorldRenderer.hpp"
#include "LegacyCatalog.hpp"
#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <set>

int main(int argc, char** argv)
{
    mcworld::OriginalWorld world(12345, 5120, true);
    struct Example { const char* type; int x, z; };
    const Example examples[] = {{"Village",1842,866}, {"DesertPyramid",2344,2216},
                                {"JunglePyramid",2728,3592}, {"SwamplandHut",1384,1368}};
    for (const auto& example : examples) {
        const auto found = world.landmarks(example.x, example.z, 80);
        auto target = found.end();
        for (auto it = found.begin(); it != found.end(); ++it)
            if (it->type == example.type) { target = it; break; }
        assert(target != found.end());
        int built = 0, maximum = 0;
        std::set<int> tileIds;
        const int cx = (target->minX + target->maxX) / 2;
        const int cz = (target->minZ + target->maxZ) / 2;
        for (int z = std::max(target->minZ,cz-20); z <= std::min(target->maxZ,cz+20); ++z)
            for (int x = std::max(target->minX,cx-20); x <= std::min(target->maxX,cx+20); ++x)
                for (int y = 1; y < 128; ++y) {
                    const auto block = world.blockAt(x,y,z);
                    if (!block) continue;
                    const auto& spec = legacy::kBlockSpecs[block-1];
                    if (spec.tileId != 1 && spec.tileId != 2 && spec.tileId != 3 && spec.tileId != 7 &&
                        spec.tileId != 8 && spec.tileId != 9 && spec.tileId != 12 && spec.tileId != 13 &&
                        spec.tileId != 79 && spec.tileId != 110) {
                        ++built; maximum = std::max(maximum,y); tileIds.insert(spec.tileId);
                    }
                }
        assert(built > 50);
        const float eyeX = float(cx)+0.5f;
        const float eyeZ = float(target->minZ)-12.5f;
        const auto volume = world.renderVolume(eyeX,eyeZ);
        std::vector<uint32_t> pixels(960*266);
        worldrender::renderVolumeFrame(pixels.data(),960,266,volume,
            {eyeX,float(maximum)+5,eyeZ},0,-18,2,1,-1);
        if (argc > 1) {
            const auto path = std::string(argv[1])+"/structure-"+example.type+".ppm";
            std::ofstream output(path,std::ios::binary);
            output << "P6\n960 266\n255\n";
            for (auto pixel : pixels) { const char rgb[]={char(pixel>>16),char(pixel>>8),char(pixel)};output.write(rgb,3); }
        }
        std::cout << example.type << " biome=" << target->biome << " logical=" << target->x << ',' << target->z
                  << " pieces=" << target->pieceCount << " sampled_building_blocks=" << built << " tileIds=";
        for (int tile : tileIds) std::cout << tile << ',';
        std::cout << '\n';
    }
}
