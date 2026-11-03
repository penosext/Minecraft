#include "World.hpp"
#include "WorldRenderer.hpp"
#include "ActorRenderer.hpp"
#include "MobRenderer.hpp"
#include "Scene.hpp"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <unistd.h>

void image(const char* path, const std::vector<uint32_t>& pixels)
{
    std::ofstream output(path, std::ios::binary);
    output << "P6\n960 266\n255\n";
    for (auto pixel : pixels) {
        const char rgb[] = {char(pixel >> 16), char(pixel >> 8), char(pixel)};
        output.write(rgb, 3);
    }
}

int main(int argc, char** argv)
{
    char temporary[] = "/tmp/mc-world-scene.XXXXXX";
    const char* root = mkdtemp(temporary);
    assert(root);
    mcworld::World world;
    world.create(12345, root, true, 5120);
    assert(world.width() == 5120 && world.height() == 256 && world.depth() == 5120);
    const auto& volume = world.renderVolume();
    assert(volume.width == 96 && volume.height == 256 && volume.depth == 96);
    mcscene::Scene scene;
    scene.open(world);
    std::vector<uint32_t> day(960 * 266), night(day.size());
    const auto started = std::chrono::steady_clock::now();
    renderWorldFrame(day.data(), 960, 266, world, 0, -10, 3, 1.0f, 0.0f);
    const auto renderTime = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    renderMobOverlay(day.data(), 960, 266, world, 0, -10, scene.mobs(), 1.0f);
    renderActorOverlay(day.data(), 960, 266, 0, 0, 0);
    renderWorldFrame(night.data(), 960, 266, world, 0, -10, 3, 0.2f, 0.5f);
    renderMobOverlay(night.data(), 960, 266, world, 0, -10, scene.mobs(), 0.2f);
    renderActorOverlay(night.data(), 960, 266, 0, 0, 0);
    assert(day != night);
    std::set<uint32_t> colors(day.begin(), day.end());
    assert(colors.size() > 100);
    uint64_t daySum = 0, nightSum = 0;
    for (size_t i = 0; i < day.size(); ++i) {
        daySum += ((day[i] >> 16) & 255) + ((day[i] >> 8) & 255) + (day[i] & 255);
        nightSum += ((night[i] >> 16) & 255) + ((night[i] >> 8) & 255) + (night[i] & 255);
    }
    assert(daySum > nightSum * 2);
    const float z = world.playerZ();
    for (int i = 0; i < 20; ++i) { world.update(0.05f, 1, 0, false, 0); scene.update(world, 0.05f); }
    assert(scene.ticks() == 20);
    assert(world.playerZ() != z);
    world.save();
    scene.save(world);
    mcworld::World restored;
    restored.open(world.path(), root);
    mcscene::Scene restoredScene;
    restoredScene.open(restored);
    assert(restoredScene.ticks() == scene.ticks());
    assert(restoredScene.mobs().size() == scene.mobs().size());
    assert(restored.playerX() == world.playerX() && restored.playerZ() == world.playerZ());
    if (argc >= 3) { image(argv[1], day); image(argv[2], night); }
    std::cout << "Large world + scene integration passed, render=" << renderTime << "ms, colors=" << colors.size()
              << ", mobs=" << scene.mobs().size() << '\n' << world.statusJson() << '\n' << scene.summaryJson() << '\n';
    std::filesystem::remove_all(root);
}
