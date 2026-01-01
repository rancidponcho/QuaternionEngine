/*
================================================================================
    Engine Core
    Manages the SDL3 lifecycle, GPU device context, and windowing.
================================================================================
*/

#include "engine.h"
#include "input.h"

#include "SDL3/SDL.h"
#include <SDL3/SDL_log.h>
#include <stdio.h>

/*
================================================================================
    Public API
================================================================================
*/

bool Engine_Init(EngineContext *ctx) {
    // -------------------------------------------------------------------------
    // Host System
    // -------------------------------------------------------------------------
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init Failed: %s", SDL_GetError());
        return false;
    }

    // Note: On iOS/Android, dimensions are ignored for fullscreen windows.
    ctx->window = SDL_CreateWindow("Quaternion", 1280, 720, SDL_WINDOW_FULLSCREEN | SDL_WINDOW_RESIZABLE);
    if (!ctx->window) {
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "Window Creation Failed: %s", SDL_GetError());
        return false;
    }

    // -------------------------------------------------------------------------
    // GPU Device
    // -------------------------------------------------------------------------
    // Debug mode (true) enables validation layers. Disable for release builds.
    ctx->gpu = SDL_CreateGPUDevice(Engine_GetShaderFormat(), true, NULL);
    if (!ctx->gpu) {
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "GPU Device Init Failed: %s", SDL_GetError());
        return false;
    }

    // -------------------------------------------------------------------------
    // Presentation
    // -------------------------------------------------------------------------
    if (!SDL_ClaimWindowForGPUDevice(ctx->gpu, ctx->window)) {
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "Swapchain Claim Failed: %s", SDL_GetError());
        return false;
    }

    // -------------------------------------------------------------------------
    // Subsystems
    // -------------------------------------------------------------------------
    Input_Init(ctx);
    ctx->lastTime = SDL_GetTicks();

    SDL_Log("SYSTEM: Engine Initialized (Format: %d)", Engine_GetShaderFormat());
    return true;
}

void Engine_Shutdown(EngineContext *ctx) {
    SDL_Log("SYSTEM: Engine Shutdown Initiated");

    if (ctx->drawTexture) {
        SDL_ReleaseGPUTexture(ctx->gpu, ctx->drawTexture);
        ctx->drawTexture = NULL;
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

void Engine_ResizeTexture(EngineContext *ctx, int w, int h) {
    // Guard: Prevent invalid or zero-sized allocations (e.g., minimized window)
    if (w <= 0 || h <= 0) {
        return;
    }

    // SYNC: We must flush the GPU pipeline before destroying resources
    // that might currently be in use by a command buffer.
    SDL_WaitForGPUIdle(ctx->gpu);

    if (ctx->drawTexture) {
        SDL_ReleaseGPUTexture(ctx->gpu, ctx->drawTexture);
    }

    // Update State
    ctx->texWidth = w;
    ctx->texHeight = h;

    // Allocate storage for the Compute Shader to write into
    SDL_GPUTextureCreateInfo texInfo = {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT, // High-precision float
        .width = ctx->texWidth,
        .height = ctx->texHeight,
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .usage = SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_TEXTUREUSAGE_SAMPLER
    };

    ctx->drawTexture = SDL_CreateGPUTexture(ctx->gpu, &texInfo);

    if (!ctx->drawTexture) {
        SDL_LogCritical(SDL_LOG_CATEGORY_RENDER, "Failed to resize VRAM texture to %dx%d", w, h);
    } else {
        SDL_Log("SYSTEM: VRAM Resized [%dx%d]", w, h);
    }
}
