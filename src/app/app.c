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

        App_HandleLifecycle(ctx);
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
