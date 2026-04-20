#include "hotload.h"
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <SDL3/SDL.h>

bool Hotload_RecompileShader(void) {
#ifdef NDEBUG
    SDL_Log("Notice: Hot-reloading is disabled in Release builds.");
    return false;
#else
    // Only working on Linux
    // Needs dxc.exe for Windows and spirv-cross for Mac
    const char* basePath = SDL_GetBasePath();
    if (!basePath) return false;

    char output[512];
    char source[512];
    char cmd[1024];

    snprintf(output, sizeof(output), "%sassets/shaders/BasicCompute.spv", basePath);
    snprintf(source, sizeof(source), "%s../src/render/shaders/BasicCompute.hlsl", basePath);
    snprintf(cmd, sizeof(cmd),
            "dxc -T cs_6_0 -E main -spirv -fspv-target-env=vulkan1.0 -Fo %s %s",
            output, source);
    
    SDL_Log("Executing: %s", cmd);
    int result = system(cmd);

    return (result == 0);
#endif
}
