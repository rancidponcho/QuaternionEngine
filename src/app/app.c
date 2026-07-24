#include "app.h"

#include "debug/debug_overlay.h"
#include "game/matter.h"
#include "game/player.h"
#include "render/renderer.h"
#include <SDL3/SDL.h>
#include <float.h>

#define PLAYER_SURFACE_SPAWN_CLEARANCE 26.0f
#define PHYSICS_DEBUG_FLAGS RENDERER_PHYSICS_DEBUG_DEFAULT

// -----------------------------------------------------------------------------
// Internal Helpers
// -----------------------------------------------------------------------------

static Vec2 App_FindPlanetSpawnPoint(const MatterWorld* world, Vec2 fallback) {
    Vec2 spawn = fallback;
    float best_surface_y = FLT_MAX;

    for (uint32_t i = 0; i < world->node_count; i++) {
        const MatterNode* node = &world->nodes[i];
        if (node->radius <= 0.0f || node->material == MATERIAL_PLAYER) {
            continue;
        }

        float surface_y = node->pos.y - node->radius;
        if (surface_y < best_surface_y) {
            best_surface_y = surface_y;
            spawn = (Vec2){node->pos.x, surface_y - PLAYER_SURFACE_SPAWN_CLEARANCE};
        }
    }

    return spawn;
}

static void App_SpawnInitialMatter(EngineContext* ctx) {
    Vec2 planet_center = {
        (float)ctx->renderer.internalW * 0.5f,
        (float)ctx->renderer.internalH * 0.56f
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

    Vec2 player_center = App_FindPlanetSpawnPoint(
        &ctx->matter,
        (Vec2){planet_center.x, planet_center.y - planet_radius * 0.84f}
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
    if (!ctx->hasWindow || ctx->isBackground) return;

    if (!Renderer_Render(ctx)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Draw failed, triggering recovery.");
        ctx->hasWindow = false;
    }
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

static void App_UpdateMining(EngineContext* ctx) {
    if (!ctx->input.mouseLeft) {
        return;
    }

    Vec2 target;
    if (!Renderer_WindowToWorldPoint(ctx, ctx->input.mousePos, &target)) {
        return;
    }

    MatterWorld_Mine(&ctx->matter, target, 12.0f, 18.0f * ctx->time.delta);
}

static void App_UpdateCamera(EngineContext* ctx) {
    Vec2 player_pos;
    if (Player_GetPosition(&ctx->player, &ctx->matter, &player_pos)) {
        Renderer_UpdateCamera(ctx, player_pos, ctx->time.delta);
    }
}

static void App_UpdateDebugInput(EngineContext* ctx) {
    if (Input_GetKeyDown(ctx, SDL_SCANCODE_F3)) {
        ctx->renderer.physicsDebugFlags ^= PHYSICS_DEBUG_FLAGS;
    }
}

static void App_Update(EngineContext* ctx) {
    App_UpdateDebugInput(ctx);
    App_UpdateMining(ctx);
    App_UpdatePlayer(ctx);

    MatterWorld_ApplyIslandGravityToMatter(&ctx->matter, ctx->time.delta);
    MatterWorld_ApplyIslandGravityToMaterial(&ctx->matter, MATERIAL_PLAYER, ctx->time.delta);

    MatterWorld_Update(&ctx->matter, ctx->time.delta);
    App_UpdateCamera(ctx);
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
