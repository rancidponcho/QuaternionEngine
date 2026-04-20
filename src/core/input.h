#ifndef CORE_INPUT_H
#define CORE_INPUT_H

/*
================================================================================
    Input System
    Manages keyboard, mouse, and system event state snapshots.
================================================================================
*/
#include "math/vec.h"
#include <SDL3/SDL.h>
#include <stdbool.h>

// Forward declaration to break circular dependency with engine.h
typedef struct EngineContext EngineContext;

// -----------------------------------------------------------------------------
// State
// -----------------------------------------------------------------------------

typedef struct InputState {
    // Virtual Sticks (normalized)
    Vec2            axisLeft;
    Vec2            axisRight;

    // Mouse / Touch
    Vec2            mousePos;
    Vec2            mouseDelta;

    // Buttons
    bool            mouseLeft;
    bool            mouseRight;

    // Keyboard (Snapshot for edge detection)
    const bool*     keyboardCurrent;     // Pointer to internal SDL state
    bool            keyboardPrevious[SDL_SCANCODE_COUNT];
} InputState;

// -----------------------------------------------------------------------------
// Lifecycle
// -----------------------------------------------------------------------------

void Input_Init(EngineContext* ctx);
void Input_Poll(EngineContext* ctx);

// -----------------------------------------------------------------------------
// Digital Logic
// -----------------------------------------------------------------------------

bool Input_GetKey(EngineContext* ctx, SDL_Scancode key);     // Held
bool Input_GetKeyDown(EngineContext* ctx, SDL_Scancode key); // Pressed (Frame)
bool Input_GetKeyUp(EngineContext* ctx, SDL_Scancode key);   // Released (Frame)

#endif // CORE_INPUT_H
