#ifndef CORE_ENGINE_H
#define CORE_ENGINE_H
/**
 * engine.h
 *
 * Defines the 'EngineContext' and other nearly-global structs
 */
#include <SDL3/SDL.h>
#include <stdbool.h>

#include "input.h"

// --- Engine Context ---
struct EngineContext {
    SDL_Window* window;
    SDL_Renderer* renderer; // TODO: swap for SDL_GPUDevice later
    InputState input;

    Uint64 lastTime;
    float deltaTime;
};

const char* GetShaderFormat();
bool Engine_Init(EngineContext *ctx);
void Engine_Shutdown(EngineContext *ctx);

#endif
