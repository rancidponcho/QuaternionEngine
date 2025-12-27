#ifndef CORE_INPUT_H
#define CORE_INPUT_H

#include <SDL3/SDL_scancode.h>
#include <stdbool.h>

typedef struct EngineContext EngineContext;

// --- Input State --- TODO: move to input.h
// This acts as a buffer between SDL's messy events and your clean game code.
typedef struct {
    // Keyboard
    const bool* keyboardCurrent; // pointer to SDL's internal state
    bool keyboardPrevious[SDL_SCANCODE_COUNT]; // store history to detect "Just Pressed"

    // Mouse
    float mouseX, mouseY;
    float mouseDeltaX, mouseDeltaY;
    bool mouseLeft, mouseRight;

    // Window Events
    bool quitRequested;
    bool windowResized;
    int windowWidth, windowHeight;
} InputState;

void Input_Init(EngineContext *ctx);
void Input_Poll(EngineContext *ctx);
bool Input_GetKey(EngineContext *ctx, SDL_Scancode key);
bool Input_GetKeyDown(EngineContext *ctx, SDL_Scancode key);
bool Input_GetKeyUp(EngineContext *ctx, SDL_Scancode key);

#endif
