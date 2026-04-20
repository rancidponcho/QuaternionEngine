/*
================================================================================
    Input System
================================================================================
*/

#include "core/input.h"
#include "core/engine.h"
#include "render/renderer.h"
#include "core/hotload.h"

#include <SDL3/SDL.h>

// -----------------------------------------------------------------------------
// Lifecycle
// -----------------------------------------------------------------------------

void Input_Init(EngineContext* ctx) {
    // SDL maintains the live state; we just grab the pointer once.
    ctx->input.keyboardCurrent = SDL_GetKeyboardState(NULL);
    
    // Zero out history to prevent startup ghost inputs
    SDL_zeroa(ctx->input.keyboardPrevious);
    
    ctx->quitRequested = false;
    ctx->input.mousePos = (Vec2){ 0.0f, 0.0f };
}

void Input_Poll(EngineContext* ctx) {
    // -------------------------------------------------------------------------
    // State Snapshot
    // -------------------------------------------------------------------------
    // Copy current -> previous to enable edge detection (Down/Up)
    SDL_memcpy(ctx->input.keyboardPrevious,
               ctx->input.keyboardCurrent,
               SDL_SCANCODE_COUNT * sizeof(bool));

    // Reset frame-specific deltas
    ctx->input.mouseDelta = (Vec2){ 0.0f, 0.0f };

    // -------------------------------------------------------------------------
    // Event Pump
    // -------------------------------------------------------------------------
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
                ctx->quitRequested = true;
                break;
            // Minimization & sleep
            case SDL_EVENT_DID_ENTER_BACKGROUND:
            case SDL_EVENT_WINDOW_MINIMIZED:
                if (ctx->hasWindow) {
                    SDL_WaitForGPUIdle(ctx->gpu);
                    SDL_ReleaseWindowFromGPUDevice(ctx->gpu, ctx->window);
                    ctx->hasWindow = false;
                }
                ctx->isBackground = true;
                break;

            case SDL_EVENT_WILL_ENTER_FOREGROUND:
            case SDL_EVENT_WINDOW_RESTORED:
                ctx->isBackground = false;
                break;

            // Resizing
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                Renderer_Resize(ctx, event.window.data1, event.window.data2);
                break;

            // Mouse
            case SDL_EVENT_MOUSE_MOTION:
                ctx->input.mousePos.x = event.motion.x;
                ctx->input.mousePos.y = event.motion.y;
                ctx->input.mouseDelta.x += event.motion.xrel;
                ctx->input.mouseDelta.y += event.motion.yrel;
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (event.button.button == SDL_BUTTON_LEFT)  ctx->input.mouseLeft = true;
                if (event.button.button == SDL_BUTTON_RIGHT) ctx->input.mouseRight = true;
                break;

            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (event.button.button == SDL_BUTTON_LEFT)  ctx->input.mouseLeft = false;
                if (event.button.button == SDL_BUTTON_RIGHT) ctx->input.mouseRight = false;
                break;

            // Hotloading
            case SDL_EVENT_KEY_DOWN:
                if (event.key.scancode == SDL_SCANCODE_F5) {
                    if(Hotload_RecompileShader()) {
                        Renderer_ReloadShader(ctx);
                    }
                }
                break;
        }
    }
}

// -----------------------------------------------------------------------------
// Queries
// -----------------------------------------------------------------------------

bool Input_GetKey(EngineContext* ctx, SDL_Scancode key) {
    return ctx->input.keyboardCurrent[key];
}

bool Input_GetKeyDown(EngineContext* ctx, SDL_Scancode key) {
    // Rising Edge: High Now + Low Before
    return ctx->input.keyboardCurrent[key] && !ctx->input.keyboardPrevious[key];
}

bool Input_GetKeyUp(EngineContext* ctx, SDL_Scancode key) {
    // Falling Edge: Low Now + High Before
    return !ctx->input.keyboardCurrent[key] && ctx->input.keyboardPrevious[key];
}
