#include "input.h"

//#include <SDL3/SDL_mouse.h>
//#include <SDL3/SDL_stdinc.h>

void Input_Init(EngineContext *ctx) {
    // Get pointer to internal SDL array
    ctx->input.keyboardCurrent = SDL_GetKeyboardState(NULL);
    
    // Clear previous array
    // SDL_zeroa is a helper that calls SDL_memset automatically
    SDL_zeroa(ctx->input.keyboardPrevious);
    
    // Init mouse/window defaults
    ctx->input.quitRequested = false;
    ctx->input.mouseX = 0;
    ctx->input.mouseY = 0;
}

void Input_Poll(EngineContext *ctx) {
    // Copy the *current* state into *previous* before SDL updates Current  
    SDL_memcpy(ctx->input.keyboardPrevious,
               ctx->input.keyboardCurrent,
               SDL_SCANCODE_COUNT * sizeof(bool));

    // Reset per-frame deltas
    ctx->input.mouseDeltaX = 0;
    ctx->input.mouseDeltaY = 0;
    ctx->input.windowResized = false;

    // Process Events
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
                ctx->input.quitRequested = true;
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

            case SDL_EVENT_WINDOW_RESIZED:
                ctx->input.windowResized = true;
                ctx->input.windowWidth = event.window.data1;
                ctx->input.windowHeight = event.window.data2;
                break;
        }
    }
}

// --- Helper Implementation ---
// Edge Detection: By saving keyboardPrevious, you can distinguish between 
// "holding W to walk" (GetKey) and "tapping Space to jump" (GetKeyDown). 

// Returns TRUE as long as the key is held down
bool Input_GetKey(EngineContext *ctx, SDL_Scancode key) {
    return ctx->input.keyboardCurrent[key];
}

// Returns TRUE only on the specific frame the key was pressed
bool Input_GetKeyDown(EngineContext *ctx, SDL_Scancode key) {
    return ctx->input.keyboardCurrent[key] && !ctx->input.keyboardPrevious[key];
}

bool Input_GetKeyUp(EngineContext *ctx, SDL_Scancode key) {
    return !ctx->input.keyboardCurrent[key] && ctx->input.keyboardPrevious[key];
}

