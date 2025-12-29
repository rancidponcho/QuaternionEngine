#ifndef RENDERER_H
#define RENDERER_H

#include "../core/engine.h"
#include "core/input.h"

bool Renderer_Init(EngineContext* ctx);
void Renderer_Shutdown(EngineContext* ctx);

void Renderer_Resize(EngineContext* ctx, int winW, int winH);
void Renderer_Draw(EngineContext* ctx);

#endif 

