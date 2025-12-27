#include "core/engine.h" // SDL3/SDL.h
#include "core/input.h"
#include <SDL3/SDL_main.h> // required for main entry point (Android)
#include <SDL3/SDL_stdinc.h>

#define PLAYER_SPEED 400.0f

float timer = 0.0f;

int main(int argc, char *argv[]) {
    EngineContext ctx = {0}; 

    if (!Engine_Init(&ctx)) {
        return 1;
    }

    // Initial Player State
    // TODO: move to GameState
    float playerX = 1280 / 2.0f;
    float playerY = 720 / 2.0f;
    float playerSize = 50.0f;
    Uint8 r = 255, g = 255, b = 255;

    bool isRunning = true;

    // --- The Game Loop ---
    while (isRunning) {
        // Time Management
        Uint64 now = SDL_GetTicks();
        ctx.deltaTime = (now - ctx.lastTime) / 1000.0f;
        ctx.lastTime = now;

        // Input Polling
        Input_Poll(&ctx);

        // Check for Quit
        if (ctx.input.quitRequested || Input_GetKeyDown(&ctx, SDL_SCANCODE_ESCAPE)) {
            isRunning = false;
        }

        // Game Logic (Movement)
        // Note: Using GetKey (continuous) for movement
        if (Input_GetKey(&ctx, SDL_SCANCODE_W)) playerY -= PLAYER_SPEED * ctx.deltaTime;
        if (Input_GetKey(&ctx, SDL_SCANCODE_S)) playerY += PLAYER_SPEED * ctx.deltaTime;
        if (Input_GetKey(&ctx, SDL_SCANCODE_A)) playerX -= PLAYER_SPEED * ctx.deltaTime;
        if (Input_GetKey(&ctx, SDL_SCANCODE_D)) playerX += PLAYER_SPEED * ctx.deltaTime;

        // Note: Use GetKeyDown (instant) for actions
        if (Input_GetKeyDown(&ctx, SDL_SCANCODE_SPACE)) {
            r = SDL_rand(256);
            g = SDL_rand(256);
            b = SDL_rand(256);
            SDL_Log("Color Changes! Frame Time: %.4f ms", ctx.deltaTime * 1000.0f);
        }

        // Render
        SDL_SetRenderDrawColor(ctx.renderer, 20, 20, 20, 255); // Background color
        SDL_RenderClear(ctx.renderer);

        // Draw Player
        SDL_FRect rect = { playerX, playerY, playerSize, playerSize };
        SDL_SetRenderDrawColor(ctx.renderer, r, g, b, 255);
        SDL_RenderFillRect(ctx.renderer, &rect);

        SDL_RenderPresent(ctx.renderer);

        timer += ctx.deltaTime;
        if (timer >= 1.0f) {
            SDL_Log("FPS: %.2f | DT: %.6f", 1.0f / ctx.deltaTime, ctx.deltaTime);
            timer = 0.0f;
        }
    }
    
    Engine_Shutdown(&ctx);
    return 0;
}
