#include "app.h"

#include "debug/debug_overlay.h"
#include "game/matter.h"
#include "game/mining_tool.h"
#include "game/player.h"
#include "render/renderer.h"
#include <SDL3/SDL.h>

#define PLAYER_SURFACE_SPAWN_CLEARANCE 26.0f
#define PHYSICS_DEBUG_FLAGS RENDERER_PHYSICS_DEBUG_ALL

// -----------------------------------------------------------------------------
// Internal Helpers
// -----------------------------------------------------------------------------

static float App_ElapsedMs(Uint64 start, Uint64 end) {
    return (float)(((double)(end - start) * 1000.0) / (double)SDL_GetPerformanceFrequency());
}

static void App_HideMiningTool(EngineContext* ctx) {
    Player_ResetTool(&ctx->player);
    Renderer_ClearMiningOverlay(ctx);
}

static void App_SpawnInitialMatter(EngineContext* ctx) {
    Vec2 view_size = Renderer_GetInternalSize(ctx);
    Vec2 planet_center = {
        view_size.x * 0.5f,
        view_size.y * 0.56f
    };

    const float planet_radius = 185.0f;
    Uint64 launch_seed = SDL_GetPerformanceCounter();
    uint32_t seed = (uint32_t)(launch_seed ^ (launch_seed >> 32));

    MatterWorld_GeneratePlanet(
        &ctx->matter,
        planet_center,
        planet_radius,
        MAX_MATTER_NODES - PLAYER_BODY_NODE_COUNT,
        seed
    );

    Vec2 player_center = MatterWorld_FindSurfaceSpawnPoint(
        &ctx->matter,
        (Vec2){planet_center.x, planet_center.y - planet_radius * 0.84f},
        PLAYER_SURFACE_SPAWN_CLEARANCE
    );

    if (Player_Spawn(&ctx->player, &ctx->matter, player_center)) {
        Vec2 player_pos;
        if (Player_GetPosition(&ctx->player, &ctx->matter, &player_pos)) {
            Renderer_SetCameraCenter(ctx, player_pos);
        }
    }
}

static void App_HandleLifecycle(EngineContext* ctx) {
    if (ctx->isBackground) {
        SDL_Delay(100);
        return;
    }

    if (ctx->hasWindow) {
        return;
    }

    SDL_ReleaseWindowFromGPUDevice(ctx->gpu, ctx->window);

    if (!SDL_ClaimWindowForGPUDevice(ctx->gpu, ctx->window)) {
        SDL_Delay(20);
        return;
    }

    int w, h;
    SDL_GetWindowSizeInPixels(ctx->window, &w, &h);
    Renderer_Resize(ctx, w, h);

    ctx->hasWindow = true;
    SDL_Log("SYSTEM: Window Recovered");
}

static void App_Render(EngineContext* ctx) {
    if (!ctx->hasWindow || ctx->isBackground) {
        ctx->profile.render_ms = 0.0f;
        return;
    }

    Uint64 start = SDL_GetPerformanceCounter();
    if (!Renderer_Render(ctx)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Draw failed, triggering recovery.");
        ctx->hasWindow = false;
    }
    ctx->profile.render_ms = App_ElapsedMs(start, SDL_GetPerformanceCounter());
}

static void App_UpdatePlayer(EngineContext* ctx) {
    Vec2 target = {0.0f, 0.0f};
    bool move_active = ctx->input.mouseRight &&
        Renderer_WindowToWorldPoint(ctx, ctx->input.mousePos, &target);

    Player_Update(
        &ctx->player,
        &ctx->matter,
        move_active,
        target,
        ctx->time.delta
    );
}

static void App_UpdateToolAndMining(EngineContext* ctx) {
    ctx->profile.mining_nodes = 0;
    ctx->profile.mining_area = 0.0f;
    ctx->profile.mining_hit = false;
    ctx->profile.mining_material = MATERIAL_COUNT;

    Vec2 target;
    if (!Renderer_WindowToWorldPoint(ctx, ctx->input.mousePos, &target)) {
        App_HideMiningTool(ctx);
        return;
    }

    bool firing = ctx->input.mouseLeft;
    if (!Player_UpdateTool(&ctx->player, &ctx->matter, target, firing, ctx->time.delta)) {
        App_HideMiningTool(ctx);
        return;
    }

    const PlayerTool* tool = Player_GetTool(&ctx->player);
    Vec2 tool_direction = Player_ToolDirection(tool);
    Renderer_SetMiningTool(ctx, tool->rear, tool->muzzle, true, firing);
    MiningToolFrame mining = MiningTool_Update(
        &ctx->matter,
        Player_GetCanister(&ctx->player),
        tool->muzzle,
        tool_direction,
        tool->active,
        firing,
        ctx->time.delta
    );

    ctx->profile.mining_nodes = mining.affected_nodes;
    ctx->profile.mining_area = mining.removed_area;
    ctx->profile.mining_hit = mining.hit;
    ctx->profile.mining_material = mining.hit_material;
    Renderer_SetMiningBeam(ctx, mining.beam_start, mining.beam_end, mining.beam_active, mining.hit);
}

static void App_UpdateCamera(EngineContext* ctx) {
    Vec2 player_pos;
    if (Player_GetPosition(&ctx->player, &ctx->matter, &player_pos)) {
        Renderer_UpdateCamera(ctx, player_pos, ctx->time.delta);
    }
}

static void App_UpdateDebugInput(EngineContext* ctx) {
    if (Input_GetKeyDown(ctx, SDL_SCANCODE_F3)) {
        Renderer_TogglePhysicsDebug(ctx, PHYSICS_DEBUG_FLAGS);
    }
}

static void App_Update(EngineContext* ctx) {
    Uint64 update_start = SDL_GetPerformanceCounter();

    App_UpdateDebugInput(ctx);

    Uint64 mining_start = SDL_GetPerformanceCounter();
    App_UpdateToolAndMining(ctx);
    ctx->profile.mining_ms = App_ElapsedMs(mining_start, SDL_GetPerformanceCounter());

    Uint64 player_start = SDL_GetPerformanceCounter();
    App_UpdatePlayer(ctx);
    ctx->profile.player_ms = App_ElapsedMs(player_start, SDL_GetPerformanceCounter());

    Uint64 gravity_start = SDL_GetPerformanceCounter();
    MatterWorld_ApplyIslandGravityToMatter(&ctx->matter, ctx->time.delta);
    MatterWorld_ApplyIslandGravityToMaterial(&ctx->matter, MATERIAL_PLAYER, ctx->time.delta);
    ctx->profile.gravity_ms = App_ElapsedMs(gravity_start, SDL_GetPerformanceCounter());

    Uint64 matter_start = SDL_GetPerformanceCounter();
    MatterWorld_Update(&ctx->matter, ctx->time.delta);
    ctx->profile.matter_ms = App_ElapsedMs(matter_start, SDL_GetPerformanceCounter());

    App_UpdateCamera(ctx);
    ctx->profile.update_ms = App_ElapsedMs(update_start, SDL_GetPerformanceCounter());
    DebugOverlay_Update(ctx, ctx->time.delta);
}

// -----------------------------------------------------------------------------
// Initialize
// ----------------------------------------------------------------------------

bool App_Init(EngineContext *ctx) {
    GameState_Init(&ctx->game);

    if (!Engine_Init(ctx)) {
        return false;
    }

    App_SpawnInitialMatter(ctx);

    DebugOverlay_Init(ctx);

    return true;
}

// -----------------------------------------------------------------------------
// The Main Loop
// -----------------------------------------------------------------------------

void App_Run(EngineContext* ctx) {
    ctx->time.lastTick = SDL_GetTicks();
    bool running = true;

    while (running) {
        // Time Step
        Uint64 now = SDL_GetTicks();
        ctx->time.delta = (now - ctx->time.lastTick) / 1000.0f;
        ctx->time.lastTick = now;
        ctx->time.total += ctx->time.delta;

        // The Frame Schedule
        Input_Poll(ctx);
        if (ctx->quitRequested) running = false;

        App_Update(ctx);
        App_HandleLifecycle(ctx);
        App_Render(ctx);
    }
}

// -----------------------------------------------------------------------------
// Clean Systems
// -----------------------------------------------------------------------------

void App_Shutdown(EngineContext *ctx) {
    DebugOverlay_Shutdown(ctx);
    Engine_Shutdown(ctx);
}
