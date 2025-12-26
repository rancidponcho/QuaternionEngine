#ifndef CORE_INPUT_H
#define CORE_INPUT_H

#include "engine.h"

void Input_Init(EngineContext *ctx);
void Input_Poll(EngineContext *ctx);
bool Input_GetKey(EngineContext *ctx, SDL_Scancode key);
bool Input_GetKeyDown(EngineContext *ctx, SDL_Scancode key);
bool Input_GetKeyUp(EngineContext *ctx, SDL_Scancode key);

#endif
