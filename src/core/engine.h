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

#include "render/renderer.h"
#include "assets/assets.h"
#include "core/input.h"
#include "ui/ui.h"

// -----------------------------------------------------------------------------
// Context
// -----------------------------------------------------------------------------
typedef struct EngineContext {
    // Core Systems
    SDL_GPUDevice* gpu;
    SDL_Window* window;

    // Lifecycle
    bool quitRequested;
    bool isBackground;   // Android: App is hidden/sleeping
    bool hasWindow;      // PC: Window is minimized/lost

    // Game structs
    Renderer renderer;
    Assets assets;
    UI ui;
    InputState input;

    // Time
    struct {
        float delta;
        double total;
        Uint64 lastTick;
    } time;
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

#endif // CORE_ENGINE_H
