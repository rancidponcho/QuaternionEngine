/*
================================================================================
    Engine Core
    Manages the SDL3 lifecycle, GPU device context, and windowing.
================================================================================
*/

#include <math.h>
#include <stdio.h>

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

    // Note: On iOS/Android, dimensions are ignored for fullscreen windows.
    ctx->window = SDL_CreateWindow("Quaternion", 640, 360, /*SDL_WINDOW_FULLSCREEN |*/ SDL_WINDOW_RESIZABLE);
    if (!ctx->window) {
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "Window Creation Failed: %s", SDL_GetError());
        return false;
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
    SDL_Log("SYSTEM: Engine Shutdown Initiated");

    Assets_Destroy(ctx);
    Renderer_Shutdown(ctx);

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
