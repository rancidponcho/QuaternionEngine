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
    // System
    SDL_Window* window;          // Native Window
    SDL_GPUDevice* gpu;             // Driver Context
    
    // Pipeline & Resources
    SDL_GPUComputePipeline* computePipeline; 
    SDL_GPUTexture* drawTexture;     // Compute write target (UAV)

    // Simulation State
    int                     texWidth;
    int                     texHeight;
    int                     dispatchGroupsX;
    int                     dispatchGroupsY;
    
    // Presentation
    SDL_Rect                viewport;        // Output destination

    // Input & Time
    InputState              input;
    Uint64                  lastTime;
    float                   deltaTime;
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

bool Engine_Init(EngineContext *ctx);
void Engine_Shutdown(EngineContext *ctx);

// Resizes VRAM texture. Blocks if GPU is busy.
void Engine_ResizeTexture(EngineContext *ctx, int w, int h);

#endif // CORE_ENGINE_H
