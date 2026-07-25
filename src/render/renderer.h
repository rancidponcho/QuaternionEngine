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

#include "math/vec.h"
#include "render/ui_renderer.h"

typedef struct EngineContext EngineContext;

#define RENDERER_PHYSICS_DEBUG_FIELD   0x01u
#define RENDERER_PHYSICS_DEBUG_CIRCLES 0x02u
#define RENDERER_PHYSICS_DEBUG_ALL \
    (RENDERER_PHYSICS_DEBUG_FIELD | RENDERER_PHYSICS_DEBUG_CIRCLES)
#define RENDERER_PHYSICS_DEBUG_DEFAULT 0u

typedef struct Renderer {
    SDL_GPUComputePipeline* computePipeline;
    SDL_GPUTexture* drawTexture;
    SDL_GPUBuffer* matterNodeBuffer;
    SDL_GPUTransferBuffer* matterNodeTransferBuffer;

    UIRenderer uiRenderer;

    Vec2 cameraCenter;     // World position shown at the center of the view.
    bool cameraInitialized;

    Uint32 internalW;      // Game logic size (e.g., 1280)
    Uint32 internalH;      // Game logic size (e.g., 720)
    Uint32 outputW;        // OS Window width
    Uint32 outputH;        // OS Window height
    Uint32 dispatchX;      // Derived from internalW
    Uint32 dispatchY;      // Derived from internalH
    Uint32 visibleMatterNodeCount;
    Uint32 physicsDebugFlags;

    SDL_Rect viewport;     // Letterbox destination rect
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
void Renderer_SetCameraCenter(EngineContext* ctx, Vec2 center);
void Renderer_UpdateCamera(EngineContext* ctx, Vec2 target_center, float dt);
bool Renderer_WindowToInternalPoint(const EngineContext* ctx, Vec2 window_point, Vec2* internal_point);
bool Renderer_WindowToWorldPoint(const EngineContext* ctx, Vec2 window_point, Vec2* world_point);
bool Renderer_Render(EngineContext* ctx);

#endif // RENDER_RENDERER_H
