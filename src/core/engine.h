#ifndef CORE_ENGINE_H
#define CORE_ENGINE_H

/*
================================================================================
    Engine Core
    Global state container and Hardware Abstraction Layer (HAL).
================================================================================
*/

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <stdbool.h>

#include "core/input.h"

// -----------------------------------------------------------------------------
// Context
// -----------------------------------------------------------------------------
typedef struct EngineContext {
    //
    // Core Systems
    //
    SDL_GPUDevice* gpu;
    SDL_Window* window;

    //
    // Lifecycle
    //
    bool                    quitRequested;  // Main loop kill switch
    bool                    isBackground;   // Android: App is hidden/sleeping
    bool                    hasWindow;      // PC: Window is minimized/lost

    //
    // Time
    //
    struct {
        float               delta;          // Seconds since last frame
        double              total;          // Seconds since startup
        Uint64              lastTick;
    } time;

    //
    // Resources & Pipeline
    //
    SDL_GPUComputePipeline* computePipeline;
    SDL_GPUTexture* drawTexture;    // The 1280x720 render target

    //
    // Resolution
    //
    Uint32                  internalW;      // Game logic size (e.g., 1280)
    Uint32                  internalH;      // Game logic size (e.g., 720)
    
    Uint32                  outputW;        // OS Window width
    Uint32                  outputH;        // OS Window height

    Uint32                  dispatchX;      // Derived from internalW
    Uint32                  dispatchY;      // Derived from internalH

    SDL_Rect                viewport;       // Letterbox destination rect

    //
    // Input
    //
    InputState              input;

} EngineContext;

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

/*
    Engine_GetShaderFormat
    Resolves the backend format at compile-time (SPIR-V vs MSL).
    Static inline to avoid function call overhead.
*/
static inline SDL_GPUShaderFormat Engine_GetShaderFormat(void) {
    #if defined(SDL_PLATFORM_MACOS) || defined(SDL_PLATFORM_IOS)
        return SDL_GPU_SHADERFORMAT_METALLIB;
    #else
        return SDL_GPU_SHADERFORMAT_SPIRV;
    #endif
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

bool Engine_Init(EngineContext* ctx);
void Engine_Shutdown(EngineContext* ctx);

// Resizes VRAM texture. Blocks if GPU is busy.
void Engine_ResizeTexture(EngineContext* ctx, int w, int h);

#endif // CORE_ENGINE_H
