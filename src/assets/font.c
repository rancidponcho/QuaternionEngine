#include "font.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "core/engine.h"

// Upload a raw R8 atlas to the GPU.
// The file is expected to be atlas_w * atlas_h bytes.
bool loadFont(EngineContext *ctx, Font *font, const char *path) {
    FILE *f = NULL;
    unsigned char *pixels = NULL;
    size_t size = 0;
    size_t read_count = 0;

    SDL_GPUTextureCreateInfo tex_info;
    SDL_GPUTransferBufferCreateInfo xfer_info;
    SDL_GPUTextureTransferInfo src;
    SDL_GPUTextureRegion dst;

    SDL_GPUTexture *tex = NULL;
    SDL_GPUTransferBuffer *xfer = NULL;
    SDL_GPUCommandBuffer *cmd = NULL;
    SDL_GPUCopyPass *copy = NULL;
    void *mapped = NULL;

    assert(ctx);
    assert(ctx->gpu);
    assert(font);
    assert(path);
    assert(font->atlas_w > 0);
    assert(font->atlas_h > 0);

    size = (size_t)font->atlas_w * (size_t)font->atlas_h;

    f = fopen(path, "rb");
    if (!f) {
        SDL_Log("loadFont: failed to open %s", path);
        return false;
    }

    pixels = malloc(size);
    if (!pixels) {
        fclose(f);
        return false;
    }

    read_count = fread(pixels, 1, size, f);
    fclose(f);

    if (read_count != size) {
        free(pixels);
        SDL_Log("loadFont: short read on %s", path);
        return false;
    }
    
    tex_info = (SDL_GPUTextureCreateInfo){
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_R8_UNORM,
        .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
        .width = (Uint32)font->atlas_w, 
        .height = (Uint32)font->atlas_h,
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .sample_count = SDL_GPU_SAMPLECOUNT_1,
        .props = 0
    };

    tex = SDL_CreateGPUTexture(ctx->gpu, &tex_info);
    if (!tex) {
        free(pixels);
        SDL_Log("loadFont: texture creation failed");
        return false;
    }
    
    xfer_info = (SDL_GPUTransferBufferCreateInfo){
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = (Uint32)size,
        .props = 0
    };

    xfer = SDL_CreateGPUTransferBuffer(ctx->gpu, &xfer_info);
    if (!xfer) {
        SDL_ReleaseGPUTexture(ctx->gpu, tex);
        free(pixels);
        SDL_Log("loadFont: transfer buffer creation failed");
        return false;
    }
        
    mapped = SDL_MapGPUTransferBuffer(ctx->gpu, xfer, false);
    if (!mapped) {
        SDL_ReleaseGPUTransferBuffer(ctx->gpu, xfer);
        SDL_ReleaseGPUTexture(ctx->gpu, tex);
        free(pixels);
        SDL_Log("loadFont: transfer buffer map failed");
        return false;
    }

    memcpy(mapped, pixels, size);
    SDL_UnmapGPUTransferBuffer(ctx->gpu, xfer);
    free(pixels);

    cmd = SDL_AcquireGPUCommandBuffer(ctx->gpu);
    if (!cmd) {
        SDL_ReleaseGPUTransferBuffer(ctx->gpu, xfer);
        SDL_ReleaseGPUTexture(ctx->gpu, tex);
        SDL_Log("loadFont: command buffer acquire failed");
        return false;
    }

    copy = SDL_BeginGPUCopyPass(cmd);
    if (!copy) {
        SDL_CancelGPUCommandBuffer(cmd);
        SDL_ReleaseGPUTransferBuffer(ctx->gpu, xfer);
        SDL_ReleaseGPUTexture(ctx->gpu, tex);
        SDL_Log("loadFont: begin copy pass failed");
        return false;
    }
  
    src = (SDL_GPUTextureTransferInfo){
        .transfer_buffer = xfer,
        .offset = 0,
        .pixels_per_row = 0,
        .rows_per_layer = 0
    };

    dst = (SDL_GPUTextureRegion){
        .texture = tex,
        .mip_level = 0,
        .layer = 0,
        .x = 0,
        .y = 0,
        .z = 0,
        .w = (Uint32)font->atlas_w,
        .h = (Uint32)font->atlas_h,
        .d = 1
    };

    SDL_UploadToGPUTexture(copy, &src, &dst, false);
    SDL_EndGPUCopyPass(copy);

    if (!SDL_SubmitGPUCommandBuffer(cmd)) {
        SDL_ReleaseGPUTransferBuffer(ctx->gpu, xfer);
        SDL_ReleaseGPUTexture(ctx->gpu, tex);
        SDL_Log("loadFont: submit failed");
        return false;
    }
    
    SDL_ReleaseGPUTransferBuffer(ctx->gpu, xfer);

    font->atlas = tex;
    return true;
}

void destroyFont(EngineContext *ctx, Font *font) {
    if (font->atlas) {
        SDL_ReleaseGPUTexture(ctx->gpu, font->atlas);
        font->atlas = NULL;
    }

    *font = (Font){0};
}
