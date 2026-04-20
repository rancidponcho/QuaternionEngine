#ifndef RENDER_RENDERER_H
#define RENDER_RENDERER_H

/*
================================================================================
    Renderer
    High-level graphics pipeline and scene management.
================================================================================
*/

#include "core/engine.h"
#include <stdbool.h>

// -----------------------------------------------------------------------------
// Lifecycle
// -----------------------------------------------------------------------------

bool Renderer_Init(EngineContext* ctx);
void Renderer_Shutdown(EngineContext* ctx);

// -----------------------------------------------------------------------------
// Frame & State
// -----------------------------------------------------------------------------

void Renderer_Resize(EngineContext* ctx, int width, int height);
bool Renderer_Draw(EngineContext* ctx);
void Renderer_ReloadShader(EngineContext* ctx);
    

#endif // RENDER_RENDERER_H
