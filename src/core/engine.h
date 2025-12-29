#ifndef CORE_ENGINE_H
#define CORE_ENGINE_H

/**
 * @file engine.h
 * @brief Global Application State & Hardware Abstraction Layer (HAL) Context.
 *
 * Defines the primary state container (EngineContext) used to bridge the 
 * OS-level windowing system, the GPU device driver, and the application simulation.
 */

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <stdbool.h>

#include "input.h"

/* ==========================================================================
 * DATA STRUCTURES
 * ========================================================================== */

/**
 * @brief Application Context.
 * * Acts as the central hub for all persistent hardware handles.
 * Passed by pointer to nearly all subsystems to avoid global state.
 */
typedef struct EngineContext {
    // --- OS / Presentation Layer ---
    SDL_Window* window;         // Handle to the OS window manager object
    
    // --- GPU / Driver Layer ---
    SDL_GPUDevice* gpu;            // Logical connection to the graphics driver
    SDL_GPUComputePipeline* computePipeline; // Compiled shader microcode (PSO)
    
    // --- VRAM Resources ---
    SDL_GPUTexture* drawTexture;    // Intermediate offscreen texture (UAV/RW)
    
    // --- Simulation State ---
    int             texWidth;       // Internal resolution width
    int             texHeight;      // Internal resolution height
    int             dispatchGroupsX;
    int             dispatchGroupsY;

    // --- Render State ---
    SDL_Rect viewport;  // The actual destination on screen
    
    // --- Input & Timing ---
    InputState      input;          // Current frame's input snapshot
    Uint64          lastTime;       // High-resolution clock timestamp (ticks)
    float           deltaTime;      // Frame duration in seconds
} EngineContext;

/**
 * @brief Helper function to get shader format (.spv vs .msv)
 */
SDL_GPUShaderFormat GetShaderFormat();


/* ==========================================================================
 * PUBLIC API
 * ========================================================================== */

/**
 * @brief Initializes the low-level hardware abstraction layer.
 * * Performs the following boot sequence:
 * 1. Initializes SDL Video Subsystem.
 * 2. Creates the OS Window.
 * 3. Initializes the GPU Driver (Vulkan/Metal/D3D12).
 * 4. Negotiates the Swapchain (Presentation Surface).
 * 5. Allocates VRAM for offscreen rendering.
 * 6. Compiles/Loads shader pipelines.
 * * @param ctx Pointer to zero-initialized context struct.
 * @return true on successful boot, false on hardware failure.
 */
bool Engine_Init(EngineContext *ctx);

/**
 * @brief Teardown sequence.
 * * Releases all GPU handles, destroys the window, and unhooks from the driver.
 * Must be called before exiting main() to prevent driver resource leaks.
 */
void Engine_Shutdown(EngineContext *ctx);

/**
 * @brief Reallocates the internal draw texture to match new dimensions.
 * * BLOCKS EXECUTION: Calls SDL_WaitForGPUIdle() to ensure the GPU isn't
 * using the old texture before destroying it.
 * * @param w New width (must be > 0)
 * * @param h New height (must be > 0)
 */
void Engine_ResizeTexture(EngineContext *ctx, int w, int h);

#endif // CORE_ENGINE_H
