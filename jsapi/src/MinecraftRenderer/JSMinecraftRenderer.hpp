#pragma once

#include <jqutil_v2/jqutil.h>
#include <cstdint>
#include <array>
#include <vector>
#include "World.hpp"
#include "NativeTouch.hpp"
#include "Scene.hpp"
#include "Inventory.hpp"
#include "WorldJob.hpp"
#include "ChunkPump.hpp"
#include "GameActions.hpp"
#include "BlockEntities.hpp"
#include "PlayerSurvival.hpp"
#include "MiningEffects.hpp"
#include <memory>
#include <string>

using namespace JQUTIL_NS;

class JSMinecraftRenderer : public JQPublishObject
{
public:
    JSMinecraftRenderer();
    ~JSMinecraftRenderer();

    // render(surfacePtr:number, width:int, height:int, frame:int, yaw:number, pitch:number, blockIndex:int) -> boolean
    // 在 native 侧软件渲染一帧 Minecraft 画面并提交到 surface
    void render(JQFunctionInfo& info);

    // postFrame(surfacePtr:number, buffer, width:int, height:int) -> boolean
    // 将 JS 传入的 ARGB32 buffer 提交到 surface
    void postFrame(JQFunctionInfo& info);
    void beginCreateWorld(JQFunctionInfo& info);
    void beginOpenWorld(JQFunctionInfo& info);
    void worldJobStatus(JQFunctionInfo& info);
    void finishWorldJob(JQFunctionInfo& info);
    void cancelWorldJob(JQFunctionInfo& info);
    void setRenderRadius(JQFunctionInfo& info);
    void setChunkRanges(JQFunctionInfo& info);
    void chunkLoadStatus(JQFunctionInfo& info);
    void createWorld(JQFunctionInfo& info);
    void openWorld(JQFunctionInfo& info);
    void saveWorld(JQFunctionInfo& info);
    void worldInfo(JQFunctionInfo& info);
    void renderWorld(JQFunctionInfo& info);
    void mineWorld(JQFunctionInfo& info);
    void setMiningInput(JQFunctionInfo& info);
    void miningInfo(JQFunctionInfo& info);
    void setGameMode(JQFunctionInfo& info);
    void survivalInfo(JQFunctionInfo& info);
    void respawnPlayer(JQFunctionInfo& info);
    void setDifficulty(JQFunctionInfo& info);
    void eatHeld(JQFunctionInfo& info);
    void containerInfo(JQFunctionInfo& info);
    void closeContainer(JQFunctionInfo& info);
    void containerTransfer(JQFunctionInfo& info);
    void craftContainer(JQFunctionInfo& info);
    void setSceneTime(JQFunctionInfo& info);
    void setWorldRule(JQFunctionInfo& info);
    void teleportPlayer(JQFunctionInfo& info);
    void attackWorld(JQFunctionInfo& info);
    void targetInfo(JQFunctionInfo& info);
    void landmarks(JQFunctionInfo& info);
    void inventoryInfo(JQFunctionInfo& info);
    void selectInventory(JQFunctionInfo& info);
    void setCreativeSlot(JQFunctionInfo& info);
    void setCreativeItem(JQFunctionInfo& info);
    void inventoryTransfer(JQFunctionInfo& info);
    void inventoryOpen(JQFunctionInfo& info);
    void placeWorld(JQFunctionInfo& info);
    void useWorld(JQFunctionInfo& info);
    void beginTouch(JQFunctionInfo& info);
    void pollTouch(JQFunctionInfo& info);
    void stopTouch(JQFunctionInfo& info);

private:
    void* postFrameFn_ = nullptr;
    std::array<std::vector<uint32_t>, 3> frameBuffers_;
    size_t frameBufferIndex_ = 0;
    mcworld::World world_;
    mcscene::Scene scene_;
    mcinventory::Inventory inventory_;
    mcgame::GameActions gameActions_;
    mcplayer::Survival survival_;
    mccontainers::Containers containers_;
    mcfx::MiningEffects miningEffects_;
    bool miningHeld_ = false;
    float miningYaw_ = 0, miningPitch_ = 0;
    bool inventoryOpen_ = false;
    float sceneSaveElapsed_ = 0;
    mcinput::NativeTouch touch_;
    float walkPhase_ = 0;
    float swing_ = 0;
    bool swinging_ = false;
    std::unique_ptr<mcworld::WorldJob> worldJob_;
    mcworld::ChunkPump chunkPump_;
    // Minimum spacing between chunk commits. Each commit republishes terrain and
    // invalidates the lighting cache, so pacing them keeps frames smooth at the
    // cost of slower fill-in. This is a deliberate latency/stutter trade.
    static constexpr float ChunkLoadInterval = 0.25f;
    float chunkLoadCooldown_ = 0;
    std::string streamStatus_ = "{}";
    bool jobCancelled_ = false;
    void requireWorldIdle();
    void refreshStreamStatus();
    void* resolvePostFrame();
};

JSValue createMinecraftRenderer(JQModuleEnv* env);
