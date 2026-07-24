/*
================================================================================
    Engine Core
    Manages the SDL3 lifecycle, GPU device context, and windowing.
================================================================================
*/

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_log.h>

#include "engine.h"
#include "render/renderer.h"
#include "assets/assets.h"
#include "input.h"

/*
================================================================================
    Public API
================================================================================
*/

bool Engine_Init(EngineContext *ctx) {
    // Host System
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init Failed: %s", SDL_GetError());
        return false;
    }

    SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE;
    if (ctx->game.display.fullscreen) {
        window_flags |= SDL_WINDOW_FULLSCREEN;
    }

    // Note: On iOS/Android, dimensions are ignored for fullscreen windows.
    ctx->window = SDL_CreateWindow("Quaternion", 640, 360, window_flags);
    if (!ctx->window) {
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "Window Creation Failed: %s", SDL_GetError());
        return false;
    }

    if (!SDL_HideCursor()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to hide system cursor: %s", SDL_GetError());
    }

    // GPU Device
    // Debug mode enables validation layers. Disable for release builds.
    ctx->gpu = SDL_CreateGPUDevice(Engine_GetShaderFormat(), true, NULL);
    if (!ctx->gpu) {
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "GPU Device Init Failed: %s", SDL_GetError());
        return false;
    }

    // Presentation
    if (!SDL_ClaimWindowForGPUDevice(ctx->gpu, ctx->window)) {
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "Swapchain Claim Failed: %s", SDL_GetError());
        return false;
    }

    ctx->hasWindow = true;

    // Subsystems
    Input_Init(ctx);
    UI_Init(ctx);
    ctx->time.lastTick = SDL_GetTicks();
   
    if (!Assets_Init(ctx)) return false;
    if (!Renderer_Init(ctx)) return false;

    SDL_Log("SYSTEM: Engine Initialized (Format: %d)", Engine_GetShaderFormat());
    return true;
}

void Engine_Shutdown(EngineContext *ctx) {
    if (!ctx) {
        return;
    }

    SDL_Log("SYSTEM: Engine Shutdown Initiated");

    Renderer_Shutdown(ctx);
    Assets_Destroy(ctx);

    if (ctx->gpu && ctx->window && ctx->hasWindow) {
        SDL_ReleaseWindowFromGPUDevice(ctx->gpu, ctx->window);
        ctx->hasWindow = false;
    }

    if (ctx->gpu) {
        SDL_DestroyGPUDevice(ctx->gpu);
        ctx->gpu = NULL;
    }

    if (ctx->window) {
        SDL_DestroyWindow(ctx->window);
        ctx->window = NULL;
    }

    SDL_Quit();
}
