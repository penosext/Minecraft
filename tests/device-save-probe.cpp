#include "World.hpp"
#include "Inventory.hpp"
#include "Scene.hpp"
#include "WorldRenderer.hpp"
#include "ActorRenderer.hpp"
#include "InventoryRenderer.hpp"
#include "LegacyCatalog.hpp"
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <string_view>
#include <vector>

int main(int argc, char** argv)
{
    if (argc < 3) return 2;
    mcworld::World world;
    world.open(std::string(argv[1]) + "/world", argv[1]);
    mcinventory::Inventory inventory;
    inventory.open(world);
    mcscene::Scene scene;
    scene.open(world);
    const auto& volume = world.renderVolume();
    int logs = 0, leaves = 0, plants = 0;
    for (size_t i = 0; i < size_t(volume.width) * volume.height * volume.depth; ++i) {
        const auto voxel = volume.data[i];
        if (!voxel) continue;
        const auto& block = legacy::kBlockSpecs[voxel - 1];
        logs += block.tileId == 17;
        leaves += block.tileId == 18;
        plants += std::string_view(block.shape) == "cross";
    }
    std::vector<uint32_t> pixels(960 * 266);
    worldrender::LightingSettings lighting;
    lighting.moonPhase = int((scene.ticks() / 24000) % 8);
    auto render = [&] {
        renderWorldFrame(pixels.data(),960,266,world,0,-10,3,scene.daylight(),scene.timeOfDay(),lighting);
        renderActorOverlay(pixels.data(),960,266,0,0,0,inventory.selectedBlockIndex());
        mcinventory::renderInventoryOverlay(pixels.data(),960,266,inventory,false);
    };
    render();
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 10; ++i) render();
    const double ms = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/10;
    std::ofstream output(argv[2],std::ios::binary);
    output << "P6\n960 266\n255\n";
    for (const auto pixel:pixels) {
        const char rgb[] = {char(pixel>>16),char(pixel>>8),char(pixel)};
        output.write(rgb,3);
    }
    std::cout << "Device save reload passed. Warm host frame " << ms << "ms; resident logs=" << logs
              << ", leaves=" << leaves << ", plants=" << plants << '\n';
    std::cout << world.statusJson() << '\n' << inventory.infoJson() << '\n';
    assert(world.decorationsEnabled() && world.width()==5120 && inventory.selectedCount()==64);
}
