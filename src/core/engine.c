#include "engine.h"
#include "SDL3/SDL_gpu.h"
#include "input.h"

#include <stdio.h> // snprintf

/**
 * @brief Maps platform to SDL GPU Backend Format.
 */
SDL_GPUShaderFormat GetShaderFormat() {
    #if defined(SDL_PLATFORM_MACOS) || defined(SDL_PLATFORM_IOS)
        return SDL_GPU_SHADERFORMAT_MSL;
    #else
        return SDL_GPU_SHADERFORMAT_SPIRV;
    #endif
}

/* ==========================================================================
 * PUBLIC API IMPLEMENTATION
 * ========================================================================== */

bool Engine_Init(EngineContext *ctx) {
    
    // --------------------------------------------------------------------------
    // HOST SYSTEM INIT (Video Subsystem & Window)
    // --------------------------------------------------------------------------
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("CRITICAL: SDL_Init Failed: %s", SDL_GetError());
        return false;
    }

    ctx->window = SDL_CreateWindow("Quaternion", 1280, 720, SDL_WINDOW_RESIZABLE);
    if (!ctx->window) {
        SDL_Log("CRITICAL: Window Creation Failed: %s", SDL_GetError());
        return false;
    }

    // --------------------------------------------------------------------------
    // DEVICE INIT (GPU Driver)
    // --------------------------------------------------------------------------
    // Requests a GPU context supporting our target shader format.
    // 'true' enabled debug layers (validation) which is useful during dev.
    ctx->gpu = SDL_CreateGPUDevice(GetShaderFormat(), true, NULL);
    if(!ctx->gpu) {
        SDL_Log("CRITICAL: GPU Device Init Failed: %s", SDL_GetError());
        return false;
    }

    // --------------------------------------------------------------------------
    // PRESENTATION SETUP (Swapchain)
    // --------------------------------------------------------------------------
    // Claims the window for this GPU device, creating the underlying Surface.
    if (!SDL_ClaimWindowForGPUDevice(ctx->gpu, ctx->window)) {
        SDL_Log("CRITICAL: Failed to claim swapchain: %s", SDL_GetError());
        return false;
    }

    // Note: To unlock framerate (disable VSync), we would change swapchain params here.
    // Default is SDL_GPU_PRESENTMODE_VSYNC.
    // SDL_SetGPUSwapchainParameters(ctx->gpu, ctx->window, SDL_GPU_SWAPCHAIN_COMPOSITION_SDR, SDL_GPU_PRESENTMODE_IMMEDIATE);

    // --------------------------------------------------------------------------
    // SUBSYSTEM INIT
    // --------------------------------------------------------------------------
    Input_Init(ctx);
    ctx->lastTime = SDL_GetTicks();

    SDL_Log("SYSTEM: Engine Initialized Successfully (GPU Mode)");
    return true;
}

void Engine_Shutdown(EngineContext *ctx) {
    SDL_Log("SYSTEM: Shutting down...");
    
    // Release VRAM resources
    if (ctx->drawTexture) {
        SDL_ReleaseGPUTexture(ctx->gpu, ctx->drawTexture);
    }
    
    // Release Pipeline State Objects
    if (ctx->computePipeline) {
        SDL_ReleaseGPUComputePipeline(ctx->gpu, ctx->computePipeline);
    }
    
    // Detach from Driver and OS
    if (ctx->gpu) {
        SDL_DestroyGPUDevice(ctx->gpu);
    }
    if (ctx->window) {
        SDL_DestroyWindow(ctx->window);
    }
    
    SDL_Quit();
}

void Engine_ResizeTexture(EngineContext *ctx, int w, int h) {
    // Safety: Don't allocate 0-sized textures (minimized window)
    if (w <= 0 || h <= 0) return;

    // 1. HARDWARE SYNC
    // We cannot destroy a resource while the GPU Command Processor is potentially
    // reading/writing to it. We must flush the pipeline and wait for idle.
    SDL_WaitForGPUIdle(ctx->gpu);

    // 2. RELEASE OLD RESOURCE
    if (ctx->drawTexture) {
        SDL_ReleaseGPUTexture(ctx->gpu, ctx->drawTexture);
    }

    // 3. UPDATE STATE
    ctx->texWidth = w;
    ctx->texHeight = h;

    // 4. ALLOCATE NEW RESOURCE
    SDL_GPUTextureCreateInfo texInfo = {
        .type = SDL_GPU_TEXTURETYPE_2D,
        // Ensure this matches your pipeline format!
        .format = SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT,
        .width = ctx->texWidth,
        .height = ctx->texHeight,
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .usage = SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_TEXTUREUSAGE_SAMPLER
    };

    ctx->drawTexture = SDL_CreateGPUTexture(ctx->gpu, &texInfo);

    if (!ctx->drawTexture) {
        SDL_Log("CRITICAL: Failed to resize VRAM texture.");
        // In a real embedded system, we might trigger a watchdog or fallback here.
    } else {
        SDL_Log("SYSTEM: VRAM Resized to %dx%d", w, h);
    }
}
