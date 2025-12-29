#ifndef CORE_INPUT_H
#define CORE_INPUT_H

/**
 * @file input.h
 * @brief Input Subsystem Interface.
 *
 * Provides a polling-based interface for User Interface devices.
 * It buffers raw SDL events into a clean state struct that can be safely queried 
 * during the game update loop without worrying about event queue synchronization.
 */

#include <SDL3/SDL_scancode.h>
#include <stdbool.h>

typedef struct EngineContext EngineContext;

/* ==========================================================================
 * DATA STRUCTURES
 * ========================================================================== */

typedef struct {
    // --- Keyboard State ---
    const bool* keyboardCurrent;               // Pointer to SDL's internal live array
    bool keyboardPrevious[SDL_SCANCODE_COUNT]; // History buffer for edge detection

    // --- Mouse State ---
    float mouseX, mouseY;           // Absolute screen coordinates
    float mouseDeltaX, mouseDeltaY; // Relative movement since last frame
    bool mouseLeft, mouseRight;     // Button states

    // --- System Events ---
    bool quitRequested;             // True if OS requested app closure
    // Note: Window resizing is handled via polling in the main render loop
    // to ensure the blit command always matches the physical window size.
} InputState;

/* ==========================================================================
 * PUBLIC API
 * ========================================================================== */

/**
 * @brief Initializes input buffers and sets default values.
 */
void Input_Init(EngineContext *ctx);

/**
 * @brief Flushes the OS event queue and updates the InputState struct.
 * * Must be called exactly once per frame.
 * * Handles: Keyboard, Mouse, and Quit events.
 */
void Input_Poll(EngineContext *ctx);

/**
 * @brief Level Trigger: Returns true if the key is currently held down.
 */
bool Input_GetKey(EngineContext *ctx, SDL_Scancode key);

/**
 * @brief Rising Edge: Returns true ONLY on the frame the key was initially pressed.
 */
bool Input_GetKeyDown(EngineContext *ctx, SDL_Scancode key);

/**
 * @brief Falling Edge: Returns true ONLY on the frame the key was released.
 */
bool Input_GetKeyUp(EngineContext *ctx, SDL_Scancode key);

#endif // CORE_INPUT_H
