#include <SDL3/SDL.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_main.h> // Takes over main() for platform-specific entry (Android/iOS)

#include "core/engine.h"
#include "core/input.h"
#include "render/renderer.h"

int main(int argc, char *argv[]) {    
    EngineContext ctx = {0};

    // -------------------------------------------------------------------------
    // System Initialization
    // -------------------------------------------------------------------------
    if (!Engine_Init(&ctx)) {
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "Engine initialization failed: %s", SDL_GetError());
        return 1;
    }

    if (!Renderer_Init(&ctx)) {
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "Renderer initialization failed: %s", SDL_GetError());
        return 1;
    }

    // -------------------------------------------------------------------------
    // Main Loop
    // -------------------------------------------------------------------------
    bool running = true;
    ctx.lastTime = SDL_GetTicks();

    while (running) {
        // Time Step
        Uint64 now = SDL_GetTicks();
        ctx.deltaTime = (now - ctx.lastTime) / 1000.0f;
        ctx.lastTime = now;

        // Process Input
        Input_Poll(&ctx);
        if (ctx.input.quitRequested) {
            running = false;
        }

        // Execute Frame
        Renderer_Draw(&ctx);
    }

    // -------------------------------------------------------------------------
    // Shutdown
    // -------------------------------------------------------------------------
    Renderer_Shutdown(&ctx);
    Engine_Shutdown(&ctx);
    
    return 0;
}
