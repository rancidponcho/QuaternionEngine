#ifndef RENDER_RENDERER_H
#define RENDER_RENDERER_H

/*
================================================================================
    Renderer
    High-level graphics pipeline and scene management.
================================================================================
*/

#include <stdbool.h>
#include <SDL3/SDL_gpu.h>

#include "assets/font.h"

typedef struct EngineContext EngineContext;

typedef struct Renderer {
    SDL_GPUComputePipeline* computePipeline;
    SDL_GPUComputePipeline* textOverlayPipeline;

    SDL_GPUSampler* fontSampler;
    SDL_GPUTexture* drawTexture;

    SDL_GPUBuffer* gpuTextBuffer;
    SDL_GPUTexture* gpuTextboxBuffer;

    Uint32 internalW;      // Game logic size (e.g., 1280)
    Uint32 internalH;      // Game logic size (e.g., 720)
    Uint32 outputW;        // OS Window width
    Uint32 outputH;        // OS Window height
    Uint32 dispatchX;      // Derived from internalW
    Uint32 dispatchY;      // Derived from internalH

    SDL_Rect                viewport;       // Letterbox destination rect
} Renderer;

// -----------------------------------------------------------------------------
// Lifecycle
// -----------------------------------------------------------------------------

bool Renderer_Init(EngineContext *ctx);
void Renderer_Shutdown(EngineContext *ctx);

// -----------------------------------------------------------------------------
// Frame & State
// -----------------------------------------------------------------------------

void Renderer_Resize(EngineContext* ctx, int width, int height);
bool Renderer_Draw(EngineContext* ctx);
void Renderer_ReloadShader(EngineContext* ctx);
    

#endif // RENDER_RENDERER_H
