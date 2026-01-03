#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h> // Takes over main() for platform-specific entry (Android/iOS)

#include "app/app.h"
int main(int argc, char *argv[]) {    
    EngineContext ctx = {0};

    if (!App_Init(&ctx)) return 1;
    App_Run(&ctx);
    App_Shutdown(&ctx);
    
    return 0;
}
