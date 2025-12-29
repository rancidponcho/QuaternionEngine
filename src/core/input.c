#include "input.h"
#include "SDL3/SDL_events.h"
#include "engine.h"
#include "render/renderer.h"
#include <SDL3/SDL.h>

/* ==========================================================================
 * PUBLIC API IMPLEMENTATION
 * ========================================================================== */

void Input_Init(EngineContext *ctx) {
    // SDL maintains an internal array of keystates. We get a pointer to it once.
    // This pointer remains valid for the lifetime of the application.
    ctx->input.keyboardCurrent = SDL_GetKeyboardState(NULL);
    
    // Clear the history buffer to prevent ghost inputs on startup
    // SDL_zeroa is a helper macro that calls SDL_memset
    SDL_zeroa(ctx->input.keyboardPrevious);
    
    // Initialize default values
    ctx->input.quitRequested = false;
    ctx->input.mouseX = 0.0f;
    ctx->input.mouseY = 0.0f;
}

void Input_Poll(EngineContext *ctx) {
    // 1. STATE SNAPSHOT (History)
    // Copy the *current* state into *previous* before SDL updates it.
    // This allows us to compare frames for edge detection (GetKeyDown).
    SDL_memcpy(ctx->input.keyboardPrevious,
               ctx->input.keyboardCurrent,
               SDL_SCANCODE_COUNT * sizeof(bool));

    // 2. RESET DELTAS
    // Mouse movement deltas are valid for one frame only.
    ctx->input.mouseDeltaX = 0.0f;
    ctx->input.mouseDeltaY = 0.0f;

    // 3. EVENT PUMP (ISR Equivalent)
    // Pulls all pending OS events from the queue.
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
                ctx->input.quitRequested = true;
                break;

            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                Renderer_Resize(ctx, event.window.data1, event.window.data2);
                break;

            case SDL_EVENT_MOUSE_MOTION:
                ctx->input.mouseX = event.motion.x;
                ctx->input.mouseY = event.motion.y;
                ctx->input.mouseDeltaX += event.motion.xrel;
                ctx->input.mouseDeltaY += event.motion.yrel;
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (event.button.button == SDL_BUTTON_LEFT) ctx->input.mouseLeft = true;
                if (event.button.button == SDL_BUTTON_RIGHT) ctx->input.mouseRight = true;
                break;

            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (event.button.button == SDL_BUTTON_LEFT) ctx->input.mouseLeft = false;
                if (event.button.button == SDL_BUTTON_RIGHT) ctx->input.mouseRight = false;
                break;
                
            // Note: Window resize events are deliberately ignored here.
            // We poll SDL_GetWindowSizeInPixels() in the render loop to 
            // ensure the swapchain blit is always synchronized with the 
            // current physical window dimensions.
        }
    }
}

/* ==========================================================================
 * DIGITAL INPUT LOGIC
 * ========================================================================== */

bool Input_GetKey(EngineContext *ctx, SDL_Scancode key) {
    // Simple state check
    return ctx->input.keyboardCurrent[key];
}

bool Input_GetKeyDown(EngineContext *ctx, SDL_Scancode key) {
    // RISING EDGE: Currently High AND Previously Low
    return ctx->input.keyboardCurrent[key] && !ctx->input.keyboardPrevious[key];
}

bool Input_GetKeyUp(EngineContext *ctx, SDL_Scancode key) {
    // FALLING EDGE: Currently Low AND Previously High
    return !ctx->input.keyboardCurrent[key] && ctx->input.keyboardPrevious[key];
}
