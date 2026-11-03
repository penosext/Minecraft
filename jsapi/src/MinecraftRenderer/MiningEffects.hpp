#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace mcworld { class World; }
namespace mcgame { struct MiningState; }

namespace mcfx {
// Main-thread, CPU-only compositor. Frozen integration contract:
//   tickMining(...); effects.update(world, dt, mining); // once per gameplay frame
//   renderWorld(...); effects.render(pixels,w,h,world,yaw,pitch); // before hand/UI
// render never consumes broken or advances time. Repeated broken snapshots are
// deduplicated by World::lastEdit().revision, including consecutive breaks.
// Call reset alongside resetMining on load/reload (including same path), mode
// changes and suspension. Object/path/seed/dimension/size changes reset as well.
// Full-cell cube/grass cracks only: unsupported geometry is skipped, not drawn
// as a fictitious cube. Particle textures must have a verified atlas mapping.
class MiningEffects {
public:
    static constexpr std::size_t MaxParticles = 128;
    void update(const mcworld::World&, float dt, const mcgame::MiningState&);
    void render(uint32_t*, int w, int h, const mcworld::World&, float yaw, float pitch);
    void reset();
    std::size_t particleCount() const;

private:
    struct Particle {
        float x = 0, y = 0, z = 0, vx = 0, vy = 0, vz = 0;
        float life = 0, lifetime = 0, size = 0;
        uint16_t texture = 0;
        uint8_t u = 0, v = 0;
    };
    bool bind(const mcworld::World&);
    std::array<Particle, MaxParticles> particles_ {};
    const mcworld::World* world_ = nullptr;
    std::string path_;
    uint32_t seed_ = 0, random_ = 0x6d2b79f5u;
    int dimension_ = -1, width_ = 0, height_ = 0, depth_ = 0;
    uint64_t seenEdit_ = 0, consumedBreak_ = 0;
    int x_ = 0, y_ = 0, z_ = 0, stage_ = -1;
    uint16_t block_ = 0;
    std::size_t nextParticle_ = 0;
};
} // namespace mcfx
