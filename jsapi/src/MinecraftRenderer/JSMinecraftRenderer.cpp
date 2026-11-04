#include "JSMinecraftRenderer.hpp"
#include "SoftwareRenderer.hpp"
#include "WorldRenderer.hpp"
#include "ActorRenderer.hpp"
#include "MobRenderer.hpp"
#include "InventoryRenderer.hpp"
#include "LegacyCatalog.hpp"
#include "ItemCatalog.hpp"
#include <algorithm>
#include <stdexcept>
#include <string>
#include <dlfcn.h>
#include <vector>
#include <cstdint>
#include <cmath>

namespace
{
    const char* kPostFrameSym = "_ZN7greenui11SurfaceImpl9postFrameEPviiii";
    // void greenui::SurfaceImpl::postFrame(void* pixels, int format, int stride, int width, int height)
    typedef void (*PostFrameFn)(void*, void*, int, int, int, int);
}

JSMinecraftRenderer::JSMinecraftRenderer()
{
    // Container tiles: Tile::use opens a panel; non-empty ones refuse breaking.
    gameActions_.setTileHooks({
        [this](int x, int y, int z) { return containers_.canBreak(x, y, z); },
        [this](int x, int y, int z) { containers_.removed(x, y, z); },
        [this](int x, int y, int z) { return containers_.interact(world_, x, y, z); }});
}
JSMinecraftRenderer::~JSMinecraftRenderer() = default;

void* JSMinecraftRenderer::resolvePostFrame()
{
    if (postFrameFn_)
        return postFrameFn_;

    void* fn = dlsym(RTLD_DEFAULT, kPostFrameSym);
    if (!fn)
    {
        void* h = dlopen("/usr/lib/libfalcon.so", RTLD_NOW | RTLD_GLOBAL);
        if (h)
            fn = dlsym(h, kPostFrameSym);
    }
    postFrameFn_ = fn;
    return fn;
}

void JSMinecraftRenderer::render(JQFunctionInfo& info)
{
    try
    {
        JSContext* ctx = info.GetContext();
        if (info.Length() < 3)
        {
            info.GetReturnValue().Set(JS_NewBool(ctx, false));
            return;
        }

        double sp = 0, yaw = 45, pitch = 30;
        int32_t w = 0, h = 0, frame = 0, blockIndex = 0;
        if (JS_ToFloat64(ctx, &sp, info[0]) < 0 ||
            JS_ToInt32(ctx, &w, info[1]) < 0 || JS_ToInt32(ctx, &h, info[2]) < 0 ||
            (info.Length() >= 4 && JS_ToInt32(ctx, &frame, info[3]) < 0) ||
            (info.Length() >= 5 && JS_ToFloat64(ctx, &yaw, info[4]) < 0) ||
            (info.Length() >= 6 && JS_ToFloat64(ctx, &pitch, info[5]) < 0) ||
            (info.Length() >= 7 && JS_ToInt32(ctx, &blockIndex, info[6]) < 0))
        {
            info.GetReturnValue().Set(JS_EXCEPTION);
            return;
        }

        if (!std::isfinite(sp) || sp <= 0 || sp > 9007199254740991.0 ||
            std::floor(sp) != sp || !std::isfinite(yaw) || !std::isfinite(pitch) ||
            w <= 0 || h <= 0 || w > 2048 || h > 2048)
        {
            info.GetReturnValue().Set(JS_NewBool(ctx, false));
            return;
        }

        void* fn = resolvePostFrame();
        if (!fn)
        {
            info.GetReturnValue().Set(JS_NewBool(ctx, false));
            return;
        }

        // The Surface consumer may outlive this call; keep submitted pixel storage alive.
        auto& buf = frameBuffers_[frameBufferIndex_];
        buf.resize((size_t)w * h);
        renderMinecraftFrame(buf.data(), w, h, frame, float(std::remainder(yaw, 360.0)), 
                             float(std::fmax(-80.0, std::fmin(80.0, pitch))), blockIndex);

        ((PostFrameFn)fn)((void*)(uintptr_t)sp, buf.data(), 0, w * 4, w, h);
        frameBufferIndex_ = (frameBufferIndex_ + 1) % frameBuffers_.size();
        info.GetReturnValue().Set(JS_NewBool(ctx, true));
    }
    catch (const std::exception& e)
    {
        info.GetReturnValue().ThrowInternalError(e.what());
    }
}

void JSMinecraftRenderer::postFrame(JQFunctionInfo& info)
{
    try
    {
        JSContext* ctx = info.GetContext();
        // buffer 参数：暂不支持（先由 render 在 native 侧生成帧）
        info.GetReturnValue().Set(JS_NewBool(ctx, false));
    }
    catch (const std::exception& e)
    {
        info.GetReturnValue().ThrowInternalError(e.what());
    }
}

void JSMinecraftRenderer::requireWorldIdle()
{
    if (worldJob_ && worldJob_->active()) throw std::runtime_error("World creation/loading is still running");
    chunkPump_.poll();
}

void JSMinecraftRenderer::refreshStreamStatus()
{
    streamStatus_ = world_.loaded() ? world_.streamingInfoJson() : std::string("{}");
}

void JSMinecraftRenderer::saveAll()
{
    world_.save();
    scene_.save(world_);
    inventory_.save(world_);
    survival_.save(world_);
    containers_.save(world_);
    sceneSaveElapsed_ = 0;
}

void JSMinecraftRenderer::adoptWorld(mcworld::World&& world, mcscene::Scene&& scene,
                                     mcinventory::Inventory&& inventory)
{
    // Open the per-world stores against the candidate first: if either throws,
    // the currently loaded world stays intact.
    mcplayer::Survival survival;
    survival.open(world);
    mccontainers::Containers containers;
    containers.open(world);
    touch_.stop();
    world_ = std::move(world);
    scene_ = std::move(scene);
    inventory_ = std::move(inventory);
    survival_ = std::move(survival);
    containers_ = std::move(containers);
    inventoryOpen_ = false;
    miningHeld_ = false; gameActions_.resetMining();
    sceneSaveElapsed_ = 0;
    swinging_ = false; swing_ = 0;
    chunkLoadCooldown_ = 0;
    refreshStreamStatus();
}

void JSMinecraftRenderer::beginCreateWorld(JQFunctionInfo& info)
{
    try {
        requireWorldIdle();
        chunkPump_.reset();
        uint32_t seed = 0;
        int32_t size = 5120, radius = 1;
        if (info.Length() < 1) throw std::runtime_error("World seed is required");
        if (JS_ToUint32(info.GetContext(), &seed, info[0]) < 0 ||
            (info.Length() > 1 && JS_ToInt32(info.GetContext(), &size, info[1]) < 0) ||
            (info.Length() > 2 && JS_ToInt32(info.GetContext(), &radius, info[2]) < 0)) {
            info.GetReturnValue().Set(JS_EXCEPTION); return;
        }
        if (radius < 1 || radius > 12) throw std::runtime_error("Render radius must be between 1 and 12");
        if (world_.loaded()) saveAll();
        touch_.stop();
        worldJob_ = std::make_unique<mcworld::WorldJob>();
        jobCancelled_ = false;
        const bool started = worldJob_->startCreate(seed, size, radius);
        info.GetReturnValue().Set(JS_NewBool(info.GetContext(), started));
    } catch (const std::exception& e) { info.GetReturnValue().ThrowInternalError(e.what()); }
}

void JSMinecraftRenderer::beginOpenWorld(JQFunctionInfo& info)
{
    try {
        requireWorldIdle();
        chunkPump_.reset();
        if (info.Length() < 1) throw std::runtime_error("World path is required");
        const char* value = JS_ToCString(info.GetContext(), info[0]);
        if (!value) { info.GetReturnValue().Set(JS_EXCEPTION); return; }
        const std::string path(value); JS_FreeCString(info.GetContext(), value);
        int32_t radius = 1;
        if (info.Length() > 1 && JS_ToInt32(info.GetContext(), &radius, info[1]) < 0) {
            info.GetReturnValue().Set(JS_EXCEPTION); return;
        }
        if (radius < 1 || radius > 12) throw std::runtime_error("Render radius must be between 1 and 12");
        if (world_.loaded()) saveAll();
        touch_.stop();
        worldJob_ = std::make_unique<mcworld::WorldJob>();
        jobCancelled_ = false;
        info.GetReturnValue().Set(JS_NewBool(info.GetContext(), worldJob_->startOpen(path, radius)));
    } catch (const std::exception& e) { info.GetReturnValue().ThrowInternalError(e.what()); }
}

void JSMinecraftRenderer::worldJobStatus(JQFunctionInfo& info)
{
    try {
        std::string result = worldJob_ ? worldJob_->pollJson() : std::string("{\"state\":\"idle\",\"stage\":\"idle\",\"progress\":0,\"loadedChunks\":0,\"totalChunks\":0}");
        if (jobCancelled_ && worldJob_ && !worldJob_->active()) {
            worldJob_.reset();
            result = "{\"state\":\"cancelled\",\"stage\":\"cancelled\",\"progress\":0,\"loadedChunks\":0,\"totalChunks\":0}";
        }
        info.GetReturnValue().Set(JS_NewString(info.GetContext(), result.c_str()));
    } catch (const std::exception& e) { info.GetReturnValue().ThrowInternalError(e.what()); }
}

void JSMinecraftRenderer::finishWorldJob(JQFunctionInfo& info)
{
    try {
        if (!worldJob_ || jobCancelled_ || !worldJob_->ready()) throw std::runtime_error("World job is not ready");
        auto result = worldJob_->takeResult();
        if (!result) throw std::runtime_error("World job has no result");
        adoptWorld(std::move(result->world), std::move(result->scene), std::move(result->inventory));
        worldJob_.reset();
        const auto status = world_.statusJson();
        info.GetReturnValue().Set(JS_NewString(info.GetContext(), status.c_str()));
    } catch (const std::exception& e) { info.GetReturnValue().ThrowInternalError(e.what()); }
}

void JSMinecraftRenderer::cancelWorldJob(JQFunctionInfo& info)
{
    jobCancelled_ = true;
    if (worldJob_) worldJob_->cancel();
    info.GetReturnValue().Set(JS_NewBool(info.GetContext(), true));
}

void JSMinecraftRenderer::setRenderRadius(JQFunctionInfo& info)
{
    try {
        requireWorldIdle();
        int32_t radius = 1;
        if (info.Length() < 1) throw std::runtime_error("Render radius is required");
        if (JS_ToInt32(info.GetContext(), &radius, info[0]) < 0) { info.GetReturnValue().Set(JS_EXCEPTION); return; }
        if (chunkPump_.poll() == mcworld::ChunkPump::State::Failed) chunkPump_.reset();
        world_.beginStreaming(radius);
        refreshStreamStatus();
        info.GetReturnValue().Set(JS_NewBool(info.GetContext(), true));
    } catch (const std::exception& e) { info.GetReturnValue().ThrowInternalError(e.what()); }
}

void JSMinecraftRenderer::setChunkRanges(JQFunctionInfo& info)
{
    try {
        requireWorldIdle();
        int32_t display = 1, preload = 1;
        if (info.Length() < 2) throw std::runtime_error("Display and preload radii are required");
        if (JS_ToInt32(info.GetContext(), &display, info[0]) < 0 ||
            JS_ToInt32(info.GetContext(), &preload, info[1]) < 0) {
            info.GetReturnValue().Set(JS_EXCEPTION); return;
        }
        if (display < 1 || display > 12 || preload < display || preload > 12)
            throw std::runtime_error("Radii must satisfy 1 <= display <= preload <= 12");
        if (chunkPump_.poll() == mcworld::ChunkPump::State::Failed) chunkPump_.reset();
        world_.setChunkRanges(display, preload);
        refreshStreamStatus();
        info.GetReturnValue().Set(JS_NewBool(info.GetContext(), true));
    } catch (const std::exception& e) { info.GetReturnValue().ThrowInternalError(e.what()); }
}

void JSMinecraftRenderer::chunkLoadStatus(JQFunctionInfo& info)
{
    try {
        const auto state = chunkPump_.poll();
        const bool jobBusy = worldJob_ && worldJob_->active();
        const bool busy = jobBusy;
        const bool loading = state == mcworld::ChunkPump::State::Running;
        std::string error = state == mcworld::ChunkPump::State::Failed ? chunkPump_.error() : std::string();
        if (!jobBusy) refreshStreamStatus();
        JSContext* ctx = info.GetContext();
        JSValue object = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, object, "busy", JS_NewBool(ctx, busy));
        JS_SetPropertyStr(ctx, object, "loading", JS_NewBool(ctx, loading));
        JS_SetPropertyStr(ctx, object, "error", JS_NewString(ctx, error.c_str()));
        JSValue stream = JS_ParseJSON(ctx, streamStatus_.c_str(), streamStatus_.size(), "chunk-stream");
        if (JS_IsException(stream)) { JS_FreeValue(ctx, object); info.GetReturnValue().Set(JS_EXCEPTION); return; }
        JS_SetPropertyStr(ctx, object, "stream", stream);
        JSValue json = JS_JSONStringify(ctx, object, JS_UNDEFINED, JS_UNDEFINED);
        JS_FreeValue(ctx, object);
        info.GetReturnValue().Set(json);
    } catch (const std::exception& e) { info.GetReturnValue().ThrowInternalError(e.what()); }
}

void JSMinecraftRenderer::createWorld(JQFunctionInfo& info)
{
    try {
        requireWorldIdle();
        chunkPump_.reset();
        uint32_t seed = 0;
        if (info.Length() < 1) throw std::runtime_error("World seed is required");
        if (JS_ToUint32(info.GetContext(), &seed, info[0]) < 0) {
            info.GetReturnValue().Set(JS_EXCEPTION);
            return;
        }
        int32_t worldSize = 5120;
        if (info.Length() >= 2 && JS_ToInt32(info.GetContext(), &worldSize, info[1]) < 0) {
            info.GetReturnValue().Set(JS_EXCEPTION);
            return;
        }
        if (worldSize != 864 && worldSize != 1024 && worldSize != 3072 && worldSize != 5120)
            throw std::runtime_error("Unsupported original world size");
        if (world_.loaded()) saveAll();
        mcworld::World nextWorld;
        nextWorld.create(seed, "/tmp/mc", true, worldSize);
        mcscene::Scene nextScene;
        nextScene.open(nextWorld);
        mcinventory::Inventory nextInventory;
        nextInventory.open(nextWorld);
        adoptWorld(std::move(nextWorld), std::move(nextScene), std::move(nextInventory));
        const auto status = world_.statusJson();
        info.GetReturnValue().Set(JS_NewString(info.GetContext(), status.c_str()));
    } catch (const std::exception& e) {
        info.GetReturnValue().ThrowInternalError(e.what());
    }
}

void JSMinecraftRenderer::openWorld(JQFunctionInfo& info)
{
    try {
        requireWorldIdle();
        chunkPump_.reset();
        if (info.Length() < 1) throw std::runtime_error("World path is required");
        const char* value = JS_ToCString(info.GetContext(), info[0]);
        if (!value) {
            info.GetReturnValue().Set(JS_EXCEPTION);
            return;
        }
        const std::string path(value);
        JS_FreeCString(info.GetContext(), value);
        if (world_.loaded()) saveAll();
        mcworld::World nextWorld;
        nextWorld.open(path);
        mcscene::Scene nextScene;
        nextScene.open(nextWorld);
        mcinventory::Inventory nextInventory;
        nextInventory.open(nextWorld);
        adoptWorld(std::move(nextWorld), std::move(nextScene), std::move(nextInventory));
        const auto status = world_.statusJson();
        info.GetReturnValue().Set(JS_NewString(info.GetContext(), status.c_str()));
    } catch (const std::exception& e) {
        info.GetReturnValue().ThrowInternalError(e.what());
    }
}

void JSMinecraftRenderer::saveWorld(JQFunctionInfo& info)
{
    try {
        requireWorldIdle();
        if (!world_.loaded()) throw std::runtime_error("No world is loaded");
        saveAll();
        info.GetReturnValue().Set(JS_NewBool(info.GetContext(), true));
    } catch (const std::exception& e) {
        info.GetReturnValue().ThrowInternalError(e.what());
    }
}

void JSMinecraftRenderer::worldInfo(JQFunctionInfo& info)
{
    try {
        requireWorldIdle();
        auto status = world_.loaded() ? world_.statusJson() : std::string("{}");
        if (world_.loaded()) {
            status.pop_back();
            status += ",\"scene\":" + scene_.summaryJson() + "}";
        }
        info.GetReturnValue().Set(JS_NewString(info.GetContext(), status.c_str()));
    } catch (const std::exception& e) {
        info.GetReturnValue().ThrowInternalError(e.what());
    }
}

void JSMinecraftRenderer::renderWorld(JQFunctionInfo& info)
{
    try {
        requireWorldIdle();
        JSContext* ctx = info.GetContext();
        if (info.Length() < 9 || !world_.loaded()) {
            info.GetReturnValue().Set(JS_NewBool(ctx, false));
            return;
        }
        double surface = 0, yaw = 0, pitch = 0, forward = 0, strafe = 0, dt = 0;
        int32_t width = 0, height = 0, viewMode = 0, pixelStep = 3;
        if (JS_ToFloat64(ctx, &surface, info[0]) < 0 || JS_ToInt32(ctx, &width, info[1]) < 0 ||
            JS_ToInt32(ctx, &height, info[2]) < 0 || JS_ToFloat64(ctx, &yaw, info[3]) < 0 ||
            JS_ToFloat64(ctx, &pitch, info[4]) < 0 || JS_ToFloat64(ctx, &forward, info[5]) < 0 ||
            JS_ToFloat64(ctx, &strafe, info[6]) < 0 || JS_ToFloat64(ctx, &dt, info[8]) < 0 ||
            (info.Length() >= 10 && JS_ToInt32(ctx, &viewMode, info[9]) < 0) ||
            (info.Length() >= 11 && JS_ToInt32(ctx, &pixelStep, info[10]) < 0)) {
            info.GetReturnValue().Set(JS_EXCEPTION);
            return;
        }
        const int jump = JS_ToBool(ctx, info[7]);
        if (jump < 0) { info.GetReturnValue().Set(JS_EXCEPTION); return; }
        if (!std::isfinite(surface) || surface <= 0 || surface > 9007199254740991.0 ||
            std::floor(surface) != surface || !std::isfinite(yaw) || !std::isfinite(pitch) ||
            !std::isfinite(forward) || !std::isfinite(strafe) || !std::isfinite(dt) ||
            width <= 0 || width > 2048 || height <= 0 || height > 2048) {
            info.GetReturnValue().Set(JS_NewBool(ctx, false));
            return;
        }
        void* fn = resolvePostFrame();
        if (!fn) { info.GetReturnValue().Set(JS_NewBool(ctx, false)); return; }
        const float elapsed = float(std::clamp(dt, 0.0, 0.1));
        world_.setStreamingView(float(std::remainder(yaw, 360.0)),
                                float(std::clamp(pitch, -80.0, 80.0)), float(width) / float(height));
        world_.update(elapsed, float(std::clamp(forward, -1.0, 1.0)),
                      float(std::clamp(strafe, -1.0, 1.0)), jump != 0,
                      float(std::remainder(yaw, 360.0)));
        scene_.update(world_, elapsed);
        survival_.update(world_, elapsed, inventory_.gameMode() == mcinventory::GameMode::Creative,
                         scene_.rule(mcworld::WorldRule::NaturalRegeneration));
        containers_.update(world_, elapsed);
        const auto& mining = gameActions_.tickMining(world_, inventory_, miningYaw_, miningPitch_,
                                                    miningHeld_ && !inventoryOpen_ && !containers_.opened(), elapsed);
        miningEffects_.update(world_, elapsed, mining);
        if ((mining.active || mining.broken) && (!swinging_ || swing_ >= 0.5f)) { swing_ = 0; swinging_ = true; }
        auto& buffer = frameBuffers_[frameBufferIndex_];
        buffer.resize(size_t(width) * height);
        worldrender::LightingSettings lighting;
        lighting.moonPhase = int((scene_.ticks() / 24000) % 8);
        renderWorldFrame(buffer.data(), width, height, world_, float(std::remainder(yaw, 360.0)),
                         float(std::clamp(pitch, -80.0, 80.0)), std::clamp(pixelStep, 1, 4),
                         scene_.daylight(), scene_.timeOfDay(), lighting);
        miningEffects_.render(buffer.data(), width, height, world_,
                              float(std::remainder(yaw, 360.0)),
                              float(std::clamp(pitch, -80.0, 80.0)));
        renderMobOverlay(buffer.data(), width, height, world_, float(std::remainder(yaw, 360.0)),
                         float(std::clamp(pitch, -80.0, 80.0)), scene_.mobs(), scene_.daylight());
        walkPhase_ += elapsed * ((std::abs(forward) + std::abs(strafe)) > 0.01 ? 9.0f : 0.0f);
        if (swinging_) {
            // LivingEntity's uneffected swing duration is six 20 Hz ticks.
            swing_ += elapsed * (20.0f / 6.0f);
            if (swing_ >= 1.0f) { swing_ = 0; swinging_ = false; }
        }
        // Draw the arm first, then lay a non-block item over it. The item branch
        // used to replace the arm entirely, so holding a tool made the hand
        // disappear; the arm is part of the first-person view either way.
        // Independent Items that place a tile (cocoa beans, reeds, pot, seeds,
        // doors...) are held as their items.png sprite, never as the tile.
        const bool heldItem = viewMode != 1 && !inventory_.emptyHand() &&
                              (inventory_.selectedBlockIndex() < 0 ||
                               hasHeldItemSprite(inventory_.selectedItemId(), inventory_.selectedAux()));
        renderActorOverlay(buffer.data(), width, height, viewMode == 1 ? 1 : 0, walkPhase_, swing_,
                           heldItem ? -1 : inventory_.selectedBlockIndex());
        if (heldItem)
            renderHeldItemOverlay(buffer.data(), width, height, inventory_.selectedItemId(), inventory_.selectedAux(), swing_);
        if (containers_.opened())
            mcinventory::renderContainerOverlay(buffer.data(), width, height, inventory_, containers_.view());
        else
            mcinventory::renderInventoryOverlay(buffer.data(), width, height, inventory_, inventoryOpen_);
        ((PostFrameFn)fn)((void*)(uintptr_t)surface, buffer.data(), 0, width * 4, width, height);
        frameBufferIndex_ = (frameBufferIndex_ + 1) % frameBuffers_.size();
        sceneSaveElapsed_ += elapsed;
        // Autosave every store, including survival (health/hunger) and containers,
        // so a killed app does not restore them older than the terrain.
        if (sceneSaveElapsed_ >= 10.0f) saveAll();
        refreshStreamStatus();
        // Pace dispatches independently of whether a worker completes in one frame.
        chunkLoadCooldown_ = std::max(0.0f, chunkLoadCooldown_ - elapsed);
        if (world_.streaming() && world_.streamingInfo().queued > 0 &&
            chunkPump_.poll() != mcworld::ChunkPump::State::Failed && !chunkPump_.busy() &&
            chunkLoadCooldown_ <= 0)
            if (chunkPump_.start(world_, 1)) chunkLoadCooldown_ = ChunkLoadInterval;
        info.GetReturnValue().Set(JS_NewBool(ctx, true));
    } catch (const std::exception& e) {
        info.GetReturnValue().ThrowInternalError(e.what());
    }
}

void JSMinecraftRenderer::mineWorld(JQFunctionInfo& info)
{
    try {
        requireWorldIdle();
        double yaw = 0, pitch = 0;
        if (info.Length() < 2) throw std::runtime_error("Mining angles are required");
        if (JS_ToFloat64(info.GetContext(), &yaw, info[0]) < 0 ||
            JS_ToFloat64(info.GetContext(), &pitch, info[1]) < 0) {
            info.GetReturnValue().Set(JS_EXCEPTION);
            return;
        }
        if (!std::isfinite(yaw) || !std::isfinite(pitch)) throw std::runtime_error("Invalid mining angles");
        if (!swinging_ || swing_ >= 0.5f) { swing_ = 0; swinging_ = true; }
        const bool mined = gameActions_.tickMining(world_, inventory_, float(std::remainder(yaw, 360.0)),
                                                   float(std::clamp(pitch, -80.0, 80.0)), true, .05f).broken;
        info.GetReturnValue().Set(JS_NewBool(info.GetContext(), mined));
    } catch (const std::exception& e) {
        info.GetReturnValue().ThrowInternalError(e.what());
    }
}

void JSMinecraftRenderer::setMiningInput(JQFunctionInfo& info)
{
    try {
        if (info.Length() < 1) throw std::runtime_error("Mining held state is required");
        const int held = JS_ToBool(info.GetContext(), info[0]);
        if (held < 0) { info.GetReturnValue().Set(JS_EXCEPTION); return; }
        if (!held) {
            miningHeld_ = false; gameActions_.resetMining();
            info.GetReturnValue().Set(JS_NewBool(info.GetContext(), true)); return;
        }
        requireWorldIdle();
        double yaw = 0, pitch = 0;
        if (info.Length() < 3) throw std::runtime_error("Mining angles are required");
        if (JS_ToFloat64(info.GetContext(), &yaw, info[1]) < 0 || JS_ToFloat64(info.GetContext(), &pitch, info[2]) < 0) {
            info.GetReturnValue().Set(JS_EXCEPTION); return;
        }
        if (!world_.loaded() || !std::isfinite(yaw) || !std::isfinite(pitch))
            throw std::runtime_error("Invalid mining input");
        miningYaw_ = float(std::remainder(yaw, 360.0));
        miningPitch_ = float(std::clamp(pitch, -80.0, 80.0));
        // A container panel (chest / workbench / furnace) also suppresses
        // mining, exactly like the inventory screen.
        miningHeld_ = !inventoryOpen_ && !containers_.opened();
        info.GetReturnValue().Set(JS_NewBool(info.GetContext(), true));
    } catch (const std::exception& e) { info.GetReturnValue().ThrowInternalError(e.what()); }
}

void JSMinecraftRenderer::miningInfo(JQFunctionInfo& info)
{
    const auto result = gameActions_.miningInfoJson();
    info.GetReturnValue().Set(JS_NewString(info.GetContext(), result.c_str()));
}

void JSMinecraftRenderer::setGameMode(JQFunctionInfo& info)
{
    try {
        requireWorldIdle();
        if (!world_.loaded() || info.Length() < 1) throw std::runtime_error("A loaded world and game mode are required");
        const char* value = JS_ToCString(info.GetContext(), info[0]);
        if (!value) { info.GetReturnValue().Set(JS_EXCEPTION); return; }
        const std::string mode(value); JS_FreeCString(info.GetContext(), value);
        if (mode != "creative" && mode != "survival") throw std::runtime_error("Invalid game mode");
        auto candidate = inventory_;
        if (!candidate.setGameMode(mode == "creative" ? mcinventory::GameMode::Creative : mcinventory::GameMode::Survival))
            throw std::runtime_error("Game mode change rejected");
        world_.save();
        candidate.save(world_);
        inventory_ = std::move(candidate);
        miningHeld_ = false; gameActions_.resetMining();
        const auto result = inventory_.infoJson();
        info.GetReturnValue().Set(JS_NewString(info.GetContext(), result.c_str()));
    } catch (const std::exception& e) { info.GetReturnValue().ThrowInternalError(e.what()); }
}

void JSMinecraftRenderer::survivalInfo(JQFunctionInfo& info)
{
    info.GetReturnValue().Set(JS_NewString(info.GetContext(), survival_.infoJson().c_str()));
}
void JSMinecraftRenderer::respawnPlayer(JQFunctionInfo& info)
{
    try {
        requireWorldIdle();
        if (!world_.loaded() || !survival_.dead()) { info.GetReturnValue().Set(JS_NewBool(info.GetContext(), false)); return; }
        survival_.respawn();
        world_.teleport(survival_.spawnX(), survival_.spawnY(), survival_.spawnZ());
        survival_.save(world_);
        info.GetReturnValue().Set(JS_NewBool(info.GetContext(), true));
    } catch (const std::exception& e) { info.GetReturnValue().ThrowInternalError(e.what()); }
}
void JSMinecraftRenderer::setDifficulty(JQFunctionInfo& info)
{
    try {
        requireWorldIdle();
        int32_t value=2;
        if (info.Length()<1 || JS_ToInt32(info.GetContext(), &value, info[0])<0) { info.GetReturnValue().Set(JS_EXCEPTION); return; }
        if (!survival_.setDifficulty(value)) throw std::runtime_error("Invalid difficulty");
        survival_.save(world_);
        info.GetReturnValue().Set(JS_NewString(info.GetContext(), survival_.infoJson().c_str()));
    } catch (const std::exception& e) { info.GetReturnValue().ThrowInternalError(e.what()); }
}
void JSMinecraftRenderer::eatHeld(JQFunctionInfo& info)
{
    // Food metadata is not yet exported in the catalog; refuse rather than
    // treating an arbitrary item as food.
    info.GetReturnValue().Set(JS_NewBool(info.GetContext(), false));
}
void JSMinecraftRenderer::containerInfo(JQFunctionInfo& info)
{
    try { info.GetReturnValue().Set(JS_NewString(info.GetContext(), containers_.infoJson(inventory_).c_str())); }
    catch (const std::exception& e) { info.GetReturnValue().ThrowInternalError(e.what()); }
}
void JSMinecraftRenderer::closeContainer(JQFunctionInfo& info)
{
    try {
        requireWorldIdle();
        // Crafting grids return their items to the inventory (CraftingMenu::removed).
        if (world_.loaded()) { containers_.close(world_, inventory_); inventory_.save(world_); containers_.save(world_); }
        info.GetReturnValue().Set(JS_NewBool(info.GetContext(), true));
    } catch (const std::exception& e) { info.GetReturnValue().ThrowInternalError(e.what()); }
}
// containerTransfer(inventorySlot, containerSlot, count, toContainer); an
// inventorySlot of -1 is the shift-click quick move of containerSlot into the
// inventory. Returns {"ok":bool, ...containerInfo}.
void JSMinecraftRenderer::containerTransfer(JQFunctionInfo& info)
{
    try {
        requireWorldIdle();
        int32_t inventorySlot = -1, containerSlot = -1, count = 64;
        if (info.Length() < 2 || !world_.loaded()) throw std::runtime_error("Container slots are required");
        if (JS_ToInt32(info.GetContext(), &inventorySlot, info[0]) < 0 || JS_ToInt32(info.GetContext(), &containerSlot, info[1]) < 0 ||
            (info.Length() >= 3 && JS_ToInt32(info.GetContext(), &count, info[2]) < 0)) { info.GetReturnValue().Set(JS_EXCEPTION); return; }
        const int toContainer = info.Length() >= 4 ? JS_ToBool(info.GetContext(), info[3]) : 1;
        if (toContainer < 0) { info.GetReturnValue().Set(JS_EXCEPTION); return; }
        const bool ok = inventorySlot == -1 ? containers_.quickMove(world_, inventory_, containerSlot)
                                            : containers_.transfer(world_, inventory_, inventorySlot, containerSlot, count, toContainer != 0);
        if (ok) { inventory_.save(world_); containers_.save(world_); }
        const auto json = std::string("{\"ok\":") + (ok ? "true," : "false,") + containers_.infoJson(inventory_).substr(1);
        info.GetReturnValue().Set(JS_NewString(info.GetContext(), json.c_str()));
    } catch (const std::exception& e) { info.GetReturnValue().ThrowInternalError(e.what()); }
}
void JSMinecraftRenderer::craftContainer(JQFunctionInfo& info)
{
    try {
        requireWorldIdle();
        if (!world_.loaded()) throw std::runtime_error("No world is loaded");
        static const char* names[] = {"ok", "no-panel", "no-recipe", "inventory-full"};
        const auto result = containers_.craft(world_, inventory_);
        if (result == mccontainers::Containers::CraftResult::Ok) { inventory_.save(world_); containers_.save(world_); }
        const auto json = std::string("{\"ok\":") + (result == mccontainers::Containers::CraftResult::Ok ? "true" : "false") +
                          ",\"status\":\"" + names[int(result)] + "\"," + containers_.infoJson(inventory_).substr(1);
        info.GetReturnValue().Set(JS_NewString(info.GetContext(), json.c_str()));
    } catch (const std::exception& e) { info.GetReturnValue().ThrowInternalError(e.what()); }
}

void JSMinecraftRenderer::setSceneTime(JQFunctionInfo& info)
{
    try {
        requireWorldIdle();
        if (!world_.loaded() || info.Length() < 1) throw std::runtime_error("A loaded world and tick count are required");
        double ticks = 0;
        if (JS_ToFloat64(info.GetContext(), &ticks, info[0]) < 0) { info.GetReturnValue().Set(JS_EXCEPTION); return; }
        if (!std::isfinite(ticks) || ticks < 0 || ticks > 9.0e15) throw std::runtime_error("Invalid tick count");
        if (!scene_.setDayTicks(uint64_t(ticks))) throw std::runtime_error("Time change rejected");
        scene_.save(world_);
        const auto result = world_.statusJson();
        info.GetReturnValue().Set(JS_NewString(info.GetContext(), result.c_str()));
    } catch (const std::exception& e) { info.GetReturnValue().ThrowInternalError(e.what()); }
}

void JSMinecraftRenderer::setWorldRule(JQFunctionInfo& info)
{
    try {
        requireWorldIdle();
        if (!world_.loaded() || info.Length() < 2) throw std::runtime_error("A loaded world, rule name and value are required");
        const char* name = JS_ToCString(info.GetContext(), info[0]);
        if (!name) { info.GetReturnValue().Set(JS_EXCEPTION); return; }
        const std::string rule(name); JS_FreeCString(info.GetContext(), name);
        const int value = JS_ToBool(info.GetContext(), info[1]);
        if (value < 0) { info.GetReturnValue().Set(JS_EXCEPTION); return; }
        mcworld::WorldRule parsed;
        if (!mcworld::WorldRules::parse(rule, parsed)) throw std::runtime_error("Unknown world rule");
        if (!scene_.setRule(parsed, value != 0)) throw std::runtime_error("Rule change rejected");
        scene_.save(world_);
        const auto result = world_.statusJson();
        info.GetReturnValue().Set(JS_NewString(info.GetContext(), result.c_str()));
    } catch (const std::exception& e) { info.GetReturnValue().ThrowInternalError(e.what()); }
}

void JSMinecraftRenderer::teleportPlayer(JQFunctionInfo& info)
{
    try {
        requireWorldIdle();
        if (!world_.loaded() || info.Length() < 3) throw std::runtime_error("A loaded world and three coordinates are required");
        double x = 0, y = 0, z = 0;
        if (JS_ToFloat64(info.GetContext(), &x, info[0]) < 0 ||
            JS_ToFloat64(info.GetContext(), &y, info[1]) < 0 ||
            JS_ToFloat64(info.GetContext(), &z, info[2]) < 0) { info.GetReturnValue().Set(JS_EXCEPTION); return; }
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) throw std::runtime_error("Invalid coordinates");
        // World::teleport refuses buried or out-of-world targets itself.
        if (!world_.teleport(float(x), float(y), float(z))) throw std::runtime_error("Teleport rejected");
        miningHeld_ = false; gameActions_.resetMining();
        world_.save();
        const auto result = world_.statusJson();
        info.GetReturnValue().Set(JS_NewString(info.GetContext(), result.c_str()));
    } catch (const std::exception& e) { info.GetReturnValue().ThrowInternalError(e.what()); }
}

void JSMinecraftRenderer::attackWorld(JQFunctionInfo& info)
{
    try {
        requireWorldIdle();
        if (info.Length() < 2) throw std::runtime_error("Attack angles are required");
        double yaw = 0, pitch = 0;
        if (JS_ToFloat64(info.GetContext(), &yaw, info[0]) < 0 ||
            JS_ToFloat64(info.GetContext(), &pitch, info[1]) < 0) {
            info.GetReturnValue().Set(JS_EXCEPTION);
            return;
        }
        if (!std::isfinite(yaw) || !std::isfinite(pitch)) throw std::runtime_error("Invalid attack angles");
        if (!swinging_ || swing_ >= 0.5f) { swing_ = 0; swinging_ = true; }
        const auto result = gameActions_.attack(world_, scene_, inventory_, float(std::remainder(yaw, 360.0)),
                                                float(std::clamp(pitch, -80.0, 80.0)));
        if (result.find("\"killed\":true") != std::string::npos) {
            world_.save();
            scene_.save(world_);
            inventory_.save(world_);
            sceneSaveElapsed_ = 0;
        }
        info.GetReturnValue().Set(JS_NewString(info.GetContext(), result.c_str()));
    } catch (const std::exception& e) {
        info.GetReturnValue().ThrowInternalError(e.what());
    }
}

void JSMinecraftRenderer::targetInfo(JQFunctionInfo& info)
{
    try {
        requireWorldIdle();
        if (info.Length() < 2) throw std::runtime_error("Target angles are required");
        double yaw = 0, pitch = 0;
        if (JS_ToFloat64(info.GetContext(), &yaw, info[0]) < 0 ||
            JS_ToFloat64(info.GetContext(), &pitch, info[1]) < 0) {
            info.GetReturnValue().Set(JS_EXCEPTION);
            return;
        }
        if (!std::isfinite(yaw) || !std::isfinite(pitch)) throw std::runtime_error("Invalid target angles");
        const auto result = world_.loaded() ? scene_.targetInfo(world_, float(std::remainder(yaw, 360.0)),
                                                                float(std::clamp(pitch, -80.0, 80.0)),
                                                                inventory_.gameMode() == mcinventory::GameMode::Creative) : std::string("{}");
        info.GetReturnValue().Set(JS_NewString(info.GetContext(), result.c_str()));
    } catch (const std::exception& e) {
        info.GetReturnValue().ThrowInternalError(e.what());
    }
}

void JSMinecraftRenderer::landmarks(JQFunctionInfo& info)
{
    try {
        requireWorldIdle();
        const auto result = world_.loaded() ? world_.landmarks() : std::string("[]");
        info.GetReturnValue().Set(JS_NewString(info.GetContext(), result.c_str()));
    } catch (const std::exception& e) {
        info.GetReturnValue().ThrowInternalError(e.what());
    }
}

void JSMinecraftRenderer::inventoryInfo(JQFunctionInfo& info)
{
    try {
        requireWorldIdle();
        const auto result = inventory_.infoJson();
        info.GetReturnValue().Set(JS_NewString(info.GetContext(), result.c_str()));
    } catch (const std::exception& e) { info.GetReturnValue().ThrowInternalError(e.what()); }
}

void JSMinecraftRenderer::selectInventory(JQFunctionInfo& info)
{
    try {
        requireWorldIdle();
        int32_t slot = -1;
        if (info.Length() < 1) throw std::runtime_error("Inventory slot is required");
        if (JS_ToInt32(info.GetContext(), &slot, info[0]) < 0) { info.GetReturnValue().Set(JS_EXCEPTION); return; }
        if (slot < 0 || slot >= 9) throw std::runtime_error("Invalid hotbar slot");
        inventory_.select(slot);
        const auto result = inventory_.infoJson();
        info.GetReturnValue().Set(JS_NewString(info.GetContext(), result.c_str()));
    } catch (const std::exception& e) { info.GetReturnValue().ThrowInternalError(e.what()); }
}

void JSMinecraftRenderer::setCreativeSlot(JQFunctionInfo& info)
{
    try {
        requireWorldIdle();
        int32_t slot = -1, block = -1;
        if (info.Length() < 2) throw std::runtime_error("Creative slot and block are required");
        if (JS_ToInt32(info.GetContext(), &slot, info[0]) < 0 || JS_ToInt32(info.GetContext(), &block, info[1]) < 0) {
            info.GetReturnValue().Set(JS_EXCEPTION); return;
        }
        if (slot < 0 || slot >= 9 || block < 0 || block >= legacy::kBlockCount)
            throw std::runtime_error("Invalid creative slot or block");
        const int itemIndex = sourceitems::itemForBlockState(block);
        if (itemIndex < 0 || itemIndex >= sourceitems::kItemCount || !sourceitems::kItems[itemIndex].creative)
            throw std::runtime_error("Block state has no creative item");
        if (!inventory_.selectHotbarItem(slot, itemIndex, sourceitems::kItems[itemIndex].maxStack))
            throw std::runtime_error("Creative item selection failed");
        inventory_.save(world_);
        const auto result = inventory_.infoJson();
        info.GetReturnValue().Set(JS_NewString(info.GetContext(), result.c_str()));
    } catch (const std::exception& e) { info.GetReturnValue().ThrowInternalError(e.what()); }
}

void JSMinecraftRenderer::setCreativeItem(JQFunctionInfo& info)
{
    try {
        requireWorldIdle();
        int32_t slot = -1, itemIndex = -1;
        if (info.Length() < 2) throw std::runtime_error("Creative slot and item are required");
        if (JS_ToInt32(info.GetContext(), &slot, info[0]) < 0 || JS_ToInt32(info.GetContext(), &itemIndex, info[1]) < 0) {
            info.GetReturnValue().Set(JS_EXCEPTION); return;
        }
        if (slot < 0 || slot >= 9 || itemIndex < 0 || itemIndex >= sourceitems::kItemCount)
            throw std::runtime_error("Invalid creative slot or item");
        const auto& item = sourceitems::kItems[itemIndex];
        if (!item.creative || !inventory_.selectHotbarItem(slot, itemIndex, item.maxStack))
            throw std::runtime_error("Creative item selection failed");
        inventory_.save(world_);
        const auto result = inventory_.infoJson();
        info.GetReturnValue().Set(JS_NewString(info.GetContext(), result.c_str()));
    } catch (const std::exception& e) { info.GetReturnValue().ThrowInternalError(e.what()); }
}

void JSMinecraftRenderer::inventoryTransfer(JQFunctionInfo& info)
{
    try {
        requireWorldIdle();
        int32_t from = -1, to = -1, count = 64;
        if (info.Length() < 2) throw std::runtime_error("Inventory source and destination are required");
        if (JS_ToInt32(info.GetContext(), &from, info[0]) < 0 || JS_ToInt32(info.GetContext(), &to, info[1]) < 0 ||
            (info.Length() >= 3 && JS_ToInt32(info.GetContext(), &count, info[2]) < 0)) {
            info.GetReturnValue().Set(JS_EXCEPTION); return;
        }
        if (from < 0 || from >= 36 || to < 0 || to >= 36 || count <= 0 || count > 64)
            throw std::runtime_error("Invalid inventory transfer");
        inventory_.transfer(from, to, count);
        inventory_.save(world_);
        const auto result = inventory_.infoJson();
        info.GetReturnValue().Set(JS_NewString(info.GetContext(), result.c_str()));
    } catch (const std::exception& e) { info.GetReturnValue().ThrowInternalError(e.what()); }
}

void JSMinecraftRenderer::inventoryOpen(JQFunctionInfo& info)
{
    try {
        requireWorldIdle();
        if (info.Length() < 1 || !world_.loaded()) throw std::runtime_error("No world is loaded");
        const int open = JS_ToBool(info.GetContext(), info[0]);
        if (open < 0) { info.GetReturnValue().Set(JS_EXCEPTION); return; }
        inventoryOpen_ = open != 0;
        if (inventoryOpen_) { miningHeld_ = false; gameActions_.resetMining(); containers_.openInventory(world_); }
        else if (containers_.screen() == mccontainers::Containers::Screen::Inventory) {
            // InventoryMenu::removed: the 2x2 grid goes back to the inventory.
            containers_.close(world_, inventory_); inventory_.save(world_); containers_.save(world_);
        }
        info.GetReturnValue().Set(JS_NewBool(info.GetContext(), true));
    } catch (const std::exception& e) { info.GetReturnValue().ThrowInternalError(e.what()); }
}

void JSMinecraftRenderer::placeWorld(JQFunctionInfo& info)
{
    try {
        requireWorldIdle();
        double yaw = 0, pitch = 0;
        if (info.Length() < 2) throw std::runtime_error("Placement angles are required");
        if (JS_ToFloat64(info.GetContext(), &yaw, info[0]) < 0 || JS_ToFloat64(info.GetContext(), &pitch, info[1]) < 0) {
            info.GetReturnValue().Set(JS_EXCEPTION); return;
        }
        if (!std::isfinite(yaw) || !std::isfinite(pitch)) throw std::runtime_error("Invalid placement angles");
        const float useYaw = float(std::remainder(yaw, 360.0));
        const float usePitch = float(std::clamp(pitch, -80.0, 80.0));
        const bool acted = gameActions_.use(world_, inventory_, useYaw, usePitch);
        if (acted && (!swinging_ || swing_ >= 0.5f)) { swing_ = 0; swinging_ = true; }
        info.GetReturnValue().Set(JS_NewBool(info.GetContext(), acted));
    } catch (const std::exception& e) { info.GetReturnValue().ThrowInternalError(e.what()); }
}

void JSMinecraftRenderer::useWorld(JQFunctionInfo& info)
{
    try {
        requireWorldIdle();
        double yaw = 0, pitch = 0;
        if (info.Length() < 2) throw std::runtime_error("Interaction angles are required");
        if (JS_ToFloat64(info.GetContext(), &yaw, info[0]) < 0 || JS_ToFloat64(info.GetContext(), &pitch, info[1]) < 0) {
            info.GetReturnValue().Set(JS_EXCEPTION); return;
        }
        if (!std::isfinite(yaw) || !std::isfinite(pitch)) throw std::runtime_error("Invalid interaction angles");
        const bool used = world_.useBlock(float(std::remainder(yaw, 360.0)),
                                          float(std::clamp(pitch, -80.0, 80.0)),
                                          mcgame::GameActions::blockReach(inventory_.gameMode()));
        if (used && (!swinging_ || swing_ >= 0.5f)) { swing_ = 0; swinging_ = true; }
        info.GetReturnValue().Set(JS_NewBool(info.GetContext(), used));
    } catch (const std::exception& e) { info.GetReturnValue().ThrowInternalError(e.what()); }
}

void JSMinecraftRenderer::beginTouch(JQFunctionInfo& info)
{
    info.GetReturnValue().Set(JS_NewBool(info.GetContext(), touch_.begin()));
}

void JSMinecraftRenderer::pollTouch(JQFunctionInfo& info)
{
    try {
        const auto state = touch_.poll();
        info.GetReturnValue().Set(JS_NewString(info.GetContext(), state.c_str()));
    } catch (const std::exception& e) {
        info.GetReturnValue().ThrowInternalError(e.what());
    }
}

void JSMinecraftRenderer::stopTouch(JQFunctionInfo& info)
{
    miningHeld_ = false; gameActions_.resetMining();
    touch_.stop();
    info.GetReturnValue().Set(JS_NewBool(info.GetContext(), true));
}

JSValue createMinecraftRenderer(JQModuleEnv* env)
{
    JQFunctionTemplateRef tpl = JQFunctionTemplate::New(env, "MinecraftRenderer");
    tpl->InstanceTemplate()->setObjectCreator([]() {
        return new JSMinecraftRenderer();
    });

    tpl->SetProtoMethod("render", &JSMinecraftRenderer::render);
    tpl->SetProtoMethod("postFrame", &JSMinecraftRenderer::postFrame);
    tpl->SetProtoMethod("beginCreateWorld", &JSMinecraftRenderer::beginCreateWorld);
    tpl->SetProtoMethod("beginOpenWorld", &JSMinecraftRenderer::beginOpenWorld);
    tpl->SetProtoMethod("worldJobStatus", &JSMinecraftRenderer::worldJobStatus);
    tpl->SetProtoMethod("finishWorldJob", &JSMinecraftRenderer::finishWorldJob);
    tpl->SetProtoMethod("cancelWorldJob", &JSMinecraftRenderer::cancelWorldJob);
    tpl->SetProtoMethod("setRenderRadius", &JSMinecraftRenderer::setRenderRadius);
    tpl->SetProtoMethod("setChunkRanges", &JSMinecraftRenderer::setChunkRanges);
    tpl->SetProtoMethod("chunkLoadStatus", &JSMinecraftRenderer::chunkLoadStatus);
    tpl->SetProtoMethod("createWorld", &JSMinecraftRenderer::createWorld);
    tpl->SetProtoMethod("openWorld", &JSMinecraftRenderer::openWorld);
    tpl->SetProtoMethod("saveWorld", &JSMinecraftRenderer::saveWorld);
    tpl->SetProtoMethod("worldInfo", &JSMinecraftRenderer::worldInfo);
    tpl->SetProtoMethod("renderWorld", &JSMinecraftRenderer::renderWorld);
    tpl->SetProtoMethod("mineWorld", &JSMinecraftRenderer::mineWorld);
    tpl->SetProtoMethod("setMiningInput", &JSMinecraftRenderer::setMiningInput);
    tpl->SetProtoMethod("miningInfo", &JSMinecraftRenderer::miningInfo);
    tpl->SetProtoMethod("survivalInfo", &JSMinecraftRenderer::survivalInfo);
    tpl->SetProtoMethod("respawnPlayer", &JSMinecraftRenderer::respawnPlayer);
    tpl->SetProtoMethod("setDifficulty", &JSMinecraftRenderer::setDifficulty);
    tpl->SetProtoMethod("eatHeld", &JSMinecraftRenderer::eatHeld);
    tpl->SetProtoMethod("containerInfo", &JSMinecraftRenderer::containerInfo);
    tpl->SetProtoMethod("closeContainer", &JSMinecraftRenderer::closeContainer);
    tpl->SetProtoMethod("containerTransfer", &JSMinecraftRenderer::containerTransfer);
    tpl->SetProtoMethod("craftContainer", &JSMinecraftRenderer::craftContainer);
    tpl->SetProtoMethod("setGameMode", &JSMinecraftRenderer::setGameMode);
    tpl->SetProtoMethod("setSceneTime", &JSMinecraftRenderer::setSceneTime);
    tpl->SetProtoMethod("setWorldRule", &JSMinecraftRenderer::setWorldRule);
    tpl->SetProtoMethod("teleportPlayer", &JSMinecraftRenderer::teleportPlayer);
    tpl->SetProtoMethod("attackWorld", &JSMinecraftRenderer::attackWorld);
    tpl->SetProtoMethod("targetInfo", &JSMinecraftRenderer::targetInfo);
    tpl->SetProtoMethod("landmarks", &JSMinecraftRenderer::landmarks);
    tpl->SetProtoMethod("inventoryInfo", &JSMinecraftRenderer::inventoryInfo);
    tpl->SetProtoMethod("selectInventory", &JSMinecraftRenderer::selectInventory);
    tpl->SetProtoMethod("setCreativeSlot", &JSMinecraftRenderer::setCreativeSlot);
    tpl->SetProtoMethod("setCreativeItem", &JSMinecraftRenderer::setCreativeItem);
    tpl->SetProtoMethod("inventoryTransfer", &JSMinecraftRenderer::inventoryTransfer);
    tpl->SetProtoMethod("inventoryOpen", &JSMinecraftRenderer::inventoryOpen);
    tpl->SetProtoMethod("placeWorld", &JSMinecraftRenderer::placeWorld);
    tpl->SetProtoMethod("useWorld", &JSMinecraftRenderer::useWorld);
    tpl->SetProtoMethod("beginTouch", &JSMinecraftRenderer::beginTouch);
    tpl->SetProtoMethod("pollTouch", &JSMinecraftRenderer::pollTouch);
    tpl->SetProtoMethod("stopTouch", &JSMinecraftRenderer::stopTouch);

    JSMinecraftRenderer::InitTpl(tpl);
    return tpl->CallConstructor();
}
