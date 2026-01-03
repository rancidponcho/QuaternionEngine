#ifndef APP_H
#define APP_H

/*
================================================================================
    App
    Dedicated orchestration layer
================================================================================
*/

#include "core/engine.h"

// Initialize Engine, Renderer
bool App_Init(EngineContext* ctx);

// Enter game loop
void App_Run(EngineContext* ctx);

// Clean up systems
void App_Shutdown(EngineContext* ctx);

#endif // APP_H
