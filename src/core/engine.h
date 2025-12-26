#ifndef CORE_ENGINE_H
#define CORE_ENGINE_H
/**
 * engine.h
 *
 * Defines the 'EngineContext' and other nearly-global structs
 */
#include <SDL3/SDL.h>
#include <SDL3/SDL_video.h>
#include <stdbool.h>

// --- Input State ---
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

// --- Engine Context ---
typedef struct {
    SDL_Window* window;
    SDL_Renderer* renderer; // TODO: swap for SDL_GPUDevice later
    InputState input;

    Uint64 lastTime;
    float deltaTime;
} EngineContext;

#endif
