#include "app.h"

#include "core/engine.h"
#include "core/input.h"
#include "render/renderer.h"
#include <SDL3/SDL.h>

// -----------------------------------------------------------------------------
// Internal Helpers
// -----------------------------------------------------------------------------

// Recovers window when lost by Operating System (looking at you, Android)
static void App_HandleLifecycle(EngineContext* ctx) {
    // Background Throttling
    if (ctx->isBackground) {
        SDL_Delay(100);
        return;
    }

    // If running (!isBackground) but have no window...
    if (!ctx->hasWindow) {
        // Ensure released
        SDL_ReleaseWindowFromGPUDevice(ctx->gpu, ctx->window);

        // Try to get it back
        if (SDL_ClaimWindowForGPUDevice(ctx->gpu, ctx->window)) {
            // Sync the renderer to the new window size
            int w, h;
            SDL_GetWindowSizeInPixels(ctx->window, &w, &h);
            Renderer_Resize(ctx, w, h);
            
            ctx->hasWindow = true;
            SDL_Log("SYSTEM: Window Recovered");
        } else {
            // Wait a bit. Don't hammer the CPU/GPU driver
            SDL_Delay(20);
        }
    }
}

static void App_ProcessInput(EngineContext* ctx) {
    Input_Poll(ctx);
    // Consider adding Input_ProcessHotkeys(ctx)...
}

static void App_Update(EngineContext* ctx) {
    if (ctx->isBackground) return;

    // Network_Update(ctx);
    // Physics_Step(ctx);
    // World_UpdateEntities(ctx);
}

static void App_Render(EngineContext* ctx) {
    // Only draw on valid window
    if (!ctx->hasWindow || ctx->isBackground) return;

    if (!Renderer_Draw(ctx)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Draw failed, triggering recovery.");
        ctx->hasWindow = false;
    }
}

// -----------------------------------------------------------------------------
// Initialize
// ----------------------------------------------------------------------------

bool App_Init(EngineContext *ctx) {
    if (!Engine_Init(ctx)) {
        return false;
    }

    if (!Renderer_Init(ctx)) {
        return false;
    }

    // Network, Audio, etc...

    return true;
}

// -----------------------------------------------------------------------------
// The Main Loop
// -----------------------------------------------------------------------------

void App_Run(EngineContext* ctx) {
    ctx->lastTime = SDL_GetTicks();
    bool running = true;

    while (running) {
        // Time Step
        Uint64 now = SDL_GetTicks();
        ctx->deltaTime = (now - ctx->lastTime) / 1000.0f;
        ctx->lastTime = now;

        // The Frame Schedule
        App_ProcessInput(ctx);
        if (ctx->input.quitRequested) running = false;

        App_HandleLifecycle(ctx);
        App_Update(ctx);
        App_Render(ctx);
    }
}

// -----------------------------------------------------------------------------
// Clean Systems
// -----------------------------------------------------------------------------

void App_Shutdown(EngineContext *ctx) {
    Renderer_Shutdown(ctx);
    Engine_Shutdown(ctx);
}
