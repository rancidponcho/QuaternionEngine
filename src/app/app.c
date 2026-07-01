#include "app.h"

#include "core/engine.h"
#include "core/input.h"
#include "debug/debug_overlay.h"
#include "render/renderer.h"
#include "ui/ui.h"
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

    if (!Renderer_Render(ctx)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Draw failed, triggering recovery.");
        ctx->hasWindow = false;
    }
}

static void App_Update(EngineContext* ctx) {
    static uint32_t test_panel = UI_INVALID_ID;
    static uint32_t test_mode = 0;

    if (Input_GetKeyDown(ctx, SDL_SCANCODE_Q)) {
        if (test_panel == UI_INVALID_ID) {
            test_panel = UI_CreatePanel(&ctx->ui, 20, 20, 180);
            if (test_panel == UI_INVALID_ID) {
                return;
            }

            UI_SetPanelText(
                ctx,
                test_panel,
                "Panel controls: Q cycles position, width, colors, and rewraps this saved text."
            );
        }

        uint32_t mode = test_mode++ % 3u;
        UIStyle style = {
            .padding = 4,
            .border = 1,
            .text_color = UI_COLOR_RGBA(240, 248, 255, 255),
            .fill_color = UI_COLOR_RGBA(10, 18, 24, 190),
            .border_color = UI_COLOR_RGBA(255, 210, 96, 255)
        };
        int x = 20;
        int y = 20;
        uint16_t width = 180;

        if (mode == 1u) {
            x = 54;
            y = 34;
            width = 132;
            style.padding = 6;
            style.text_color = UI_COLOR_RGBA(255, 244, 214, 255);
            style.fill_color = UI_COLOR_RGBA(40, 18, 38, 210);
            style.border_color = UI_COLOR_RGBA(255, 124, 172, 255);
        } else if (mode == 2u) {
            x = 28;
            y = 62;
            width = 240;
            style.border = 2;
            style.text_color = UI_COLOR_RGBA(224, 255, 234, 255);
            style.fill_color = UI_COLOR_RGBA(14, 44, 38, 200);
            style.border_color = UI_COLOR_RGBA(105, 230, 170, 255);
        }

        UI_SetPanelStyle(ctx, test_panel, &style);
        UI_SetPanelWidth(ctx, test_panel, width);
        UI_SetPanelPosition(ctx, test_panel, x, y);
    }

    DebugOverlay_Update(ctx, ctx->time.delta);
}

// -----------------------------------------------------------------------------
// Initialize
// ----------------------------------------------------------------------------

bool App_Init(EngineContext *ctx) {
    if (!Engine_Init(ctx)) {
        return false;
    }

    DebugOverlay_Init(ctx);

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
