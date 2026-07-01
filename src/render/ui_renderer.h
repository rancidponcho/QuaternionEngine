#ifndef RENDER_UI_RENDERER_H
#define RENDER_UI_RENDERER_H

#include <stdbool.h>
#include <SDL3/SDL_gpu.h>

typedef struct EngineContext EngineContext;

typedef struct UIRenderer {
    SDL_GPUComputePipeline* panelPipeline;
    SDL_GPUComputePipeline* textOverlayPipeline;
    SDL_GPUSampler* fontSampler;

    SDL_GPUBuffer* panelBuffer;
    SDL_GPUBuffer* textBuffer;
    SDL_GPUBuffer* textMetaBuffer;

    SDL_GPUTransferBuffer* panelTransferBuffer;
    SDL_GPUTransferBuffer* textTransferBuffer;
    SDL_GPUTransferBuffer* textMetaTransferBuffer;
} UIRenderer;

bool UIRenderer_Init(EngineContext* ctx);
void UIRenderer_Shutdown(EngineContext* ctx);
bool UIRenderer_Render(
    EngineContext* ctx,
    SDL_GPUCommandBuffer* cmd,
    const SDL_GPUStorageTextureReadWriteBinding* outputBinding
);

#endif // RENDER_UI_RENDERER_H
