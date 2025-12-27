#include "engine.h"
#include "input.h"

const char* GetShaderFormat() {
    // SDL defines these macros automatically
    #if defined(SDL_PLATFORM_MACOS) || defined(SDL_PLATFORM_IOS)
        return "msl";
    #else
        return "spv";
    #endif
}

bool Engine_Init(EngineContext *ctx) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SSDL_Init Failed: %s", SDL_GetError());
        return false;
    }

    ctx->window = SDL_CreateWindow("Quaternion", 1280, 720, SDL_WINDOW_RESIZABLE);
    if (!ctx->window) {
        SDL_Log("Window Creation Failed: %s", SDL_GetError());
        return false;
    }

    ctx->renderer = SDL_CreateRenderer(ctx->window, NULL);
    if (!ctx->renderer) {
        SDL_Log("Renderer Creation Failed: %s", SDL_GetError());
        return false;
    }

    Input_Init(ctx);

    ctx->lastTime = SDL_GetTicks();
    return true;
}

void Engine_Shutdown(EngineContext *ctx) {
    SDL_DestroyRenderer(ctx->renderer);
    SDL_DestroyWindow(ctx->window);
    SDL_Quit();
}



    
