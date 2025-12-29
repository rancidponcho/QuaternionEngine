#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h> // Magic header for Android
#include "core/engine.h"
#include "core/input.h"
#include "render/renderer.h"

int main(int argc, char *argv[]) {
    // Force a log to prove we are alive
    SDL_Log("DEBUG: === STARTING MAIN ===");
    
    EngineContext ctx = {0};

    // CHECKPOINT 1
    SDL_Log("DEBUG: Initializing Engine...");
    if (!Engine_Init(&ctx)) {
        SDL_Log("DEBUG: Engine_Init FAILED! Error: %s", SDL_GetError());
        return 1;
    }
    SDL_Log("DEBUG: Engine_Init Success.");

    // CHECKPOINT 2
    SDL_Log("DEBUG: Initializing Renderer...");
    if (!Renderer_Init(&ctx)) {
        SDL_Log("DEBUG: Renderer_Init FAILED! Error: %s", SDL_GetError());
        return 1;
    }
    SDL_Log("DEBUG: Renderer_Init Success.");

    bool isRunning = true;
    SDL_Log("DEBUG: Entering Super Loop");

    while (isRunning) {
        Uint64 now = SDL_GetTicks();
        ctx.deltaTime = (now - ctx.lastTime) / 1000.0f;
        ctx.lastTime = now;

        Input_Poll(&ctx);
        if (ctx.input.quitRequested) {
            SDL_Log("DEBUG: Quit Requested by Input System");
            isRunning = false;
        }

        Renderer_Draw(&ctx);
    }

    Renderer_Shutdown(&ctx);
    Engine_Shutdown(&ctx);
    SDL_Log("DEBUG: === CLEAN SHUTDOWN ===");
    return 0;
}
