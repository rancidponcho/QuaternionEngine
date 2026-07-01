#include "ui_renderer.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_log.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "core/engine.h"
#include "ui/ui_internal.h"

#define TEXT_WORKGROUP_W 8
#define TEXT_WORKGROUP_H 8
#define GPU_TEXT_BUFFER_SIZE (MAX_UI_TEXT_LINES * MAX_CHARS_PER_TEXT_LINE * sizeof(uint32_t))
#define GPU_TEXT_LINE_META_BUFFER_SIZE (MAX_UI_TEXT_LINES * UITEXTLINE_META_STRIDE * sizeof(uint32_t))
#define GPU_PANEL_META_BUFFER_SIZE (MAX_UI_PANELS * UIPANEL_META_STRIDE * sizeof(uint32_t))

static void* UIRenderer_LoadFile(const char* path, size_t* outSize) {
    SDL_IOStream* io = SDL_IOFromFile(path, "rb");
    if (!io) {
        SDL_Log("UI_RENDER: Failed to open shader: %s", path);
        return NULL;
    }

    size_t size = SDL_GetIOSize(io);
    void* data = SDL_malloc(size);

    if (SDL_ReadIO(io, data, size) != size) {
        SDL_Log("UI_RENDER: Short read on shader: %s", path);
        SDL_free(data);
        SDL_CloseIO(io);
        return NULL;
    }

    SDL_CloseIO(io);
    if (outSize) *outSize = size;
    return data;
}

static const char* UIRenderer_GetShaderExtension(void) {
    #if defined(SDL_PLATFORM_MACOS) || defined(SDL_PLATFORM_IOS)
        return "metallib";
    #else
        return "spv";
    #endif
}

static SDL_GPUComputePipeline* UIRenderer_CreateTextOverlayPipeline(SDL_GPUDevice* gpu) {
    char shaderPath[256];
    const char* ext = UIRenderer_GetShaderExtension();
    const char* basePath = SDL_GetBasePath();

    if (basePath) {
        snprintf(shaderPath, sizeof(shaderPath), "%sassets/shaders/TextOverlayCompute.%s", basePath, ext);
    } else {
        snprintf(shaderPath, sizeof(shaderPath), "shaders/TextOverlayCompute.%s", ext);
    }

    size_t codeSize = 0;
    void* code = UIRenderer_LoadFile(shaderPath, &codeSize);
    if (!code) return NULL;

    const char* entryPoint = (Engine_GetShaderFormat() == SDL_GPU_SHADERFORMAT_METALLIB) ? "main0" : "main";

    SDL_GPUComputePipelineCreateInfo pipelineInfo = {
        .code = code,
        .code_size = codeSize,
        .entrypoint = entryPoint,
        .format = Engine_GetShaderFormat(),

        .num_samplers = 1,
        .num_readonly_storage_textures = 0,
        .num_readonly_storage_buffers = 2,
        .num_readwrite_storage_textures = 1,
        .num_readwrite_storage_buffers = 0,
        .num_uniform_buffers = 0,

        .threadcount_x = 8,
        .threadcount_y = 8,
        .threadcount_z = 1,
        .props = 0
    };

    SDL_GPUComputePipeline* pipeline = SDL_CreateGPUComputePipeline(gpu, &pipelineInfo);
    SDL_free(code);

    return pipeline;
}

static SDL_GPUComputePipeline* UIRenderer_CreatePanelPipeline(SDL_GPUDevice* gpu) {
    char shaderPath[256];
    const char* ext = UIRenderer_GetShaderExtension();
    const char* basePath = SDL_GetBasePath();

    if (basePath) {
        snprintf(shaderPath, sizeof(shaderPath), "%sassets/shaders/UIPanelCompute.%s", basePath, ext);
    } else {
        snprintf(shaderPath, sizeof(shaderPath), "shaders/UIPanelCompute.%s", ext);
    }

    size_t codeSize = 0;
    void* code = UIRenderer_LoadFile(shaderPath, &codeSize);
    if (!code) return NULL;

    const char* entryPoint = (Engine_GetShaderFormat() == SDL_GPU_SHADERFORMAT_METALLIB) ? "main0" : "main";

    SDL_GPUComputePipelineCreateInfo pipelineInfo = {
        .code = code,
        .code_size = codeSize,
        .entrypoint = entryPoint,
        .format = Engine_GetShaderFormat(),

        .num_samplers = 0,
        .num_readonly_storage_textures = 0,
        .num_readonly_storage_buffers = 1,
        .num_readwrite_storage_textures = 1,
        .num_readwrite_storage_buffers = 0,
        .num_uniform_buffers = 0,

        .threadcount_x = 8,
        .threadcount_y = 8,
        .threadcount_z = 1,
        .props = 0
    };

    SDL_GPUComputePipeline* pipeline = SDL_CreateGPUComputePipeline(gpu, &pipelineInfo);
    SDL_free(code);

    return pipeline;
}

static bool UIRenderer_CreateBuffers(EngineContext* ctx) {
    UIRenderer* uiRenderer = &ctx->renderer.uiRenderer;

    SDL_GPUBufferCreateInfo panelBufferInfo = {
        .usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ,
        .size = GPU_PANEL_META_BUFFER_SIZE,
        .props = 0
    };

    uiRenderer->panelBuffer = SDL_CreateGPUBuffer(ctx->gpu, &panelBufferInfo);
    if (!uiRenderer->panelBuffer) {
        SDL_LogCritical(SDL_LOG_CATEGORY_RENDER, "Failed to create GPU panel buffer: %s", SDL_GetError());
        return false;
    }

    SDL_GPUBufferCreateInfo textBufferInfo = {
        .usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ,
        .size = GPU_TEXT_BUFFER_SIZE,
        .props = 0
    };

    uiRenderer->textBuffer = SDL_CreateGPUBuffer(ctx->gpu, &textBufferInfo);
    if (!uiRenderer->textBuffer) {
        SDL_LogCritical(SDL_LOG_CATEGORY_RENDER, "Failed to create GPU text buffer: %s", SDL_GetError());
        return false;
    }

    SDL_GPUBufferCreateInfo metaBufferInfo = {
        .usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ,
        .size = GPU_TEXT_LINE_META_BUFFER_SIZE,
        .props = 0
    };

    uiRenderer->textMetaBuffer = SDL_CreateGPUBuffer(ctx->gpu, &metaBufferInfo);
    if (!uiRenderer->textMetaBuffer) {
        SDL_LogCritical(SDL_LOG_CATEGORY_RENDER, "Failed to create GPU text line meta buffer: %s", SDL_GetError());
        return false;
    }

    SDL_GPUTransferBufferCreateInfo panelTransferInfo = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = GPU_PANEL_META_BUFFER_SIZE,
        .props = 0
    };

    uiRenderer->panelTransferBuffer = SDL_CreateGPUTransferBuffer(ctx->gpu, &panelTransferInfo);
    if (!uiRenderer->panelTransferBuffer) {
        SDL_LogCritical(SDL_LOG_CATEGORY_RENDER, "Failed to create panel transfer buffer: %s", SDL_GetError());
        return false;
    }

    SDL_GPUTransferBufferCreateInfo textTransferInfo = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = GPU_TEXT_BUFFER_SIZE,
        .props = 0
    };

    uiRenderer->textTransferBuffer = SDL_CreateGPUTransferBuffer(ctx->gpu, &textTransferInfo);
    if (!uiRenderer->textTransferBuffer) {
        SDL_LogCritical(SDL_LOG_CATEGORY_RENDER, "Failed to create text transfer buffer: %s", SDL_GetError());
        return false;
    }

    SDL_GPUTransferBufferCreateInfo metaTransferInfo = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = GPU_TEXT_LINE_META_BUFFER_SIZE,
        .props = 0
    };

    uiRenderer->textMetaTransferBuffer = SDL_CreateGPUTransferBuffer(ctx->gpu, &metaTransferInfo);
    if (!uiRenderer->textMetaTransferBuffer) {
        SDL_LogCritical(SDL_LOG_CATEGORY_RENDER, "Failed to create text line meta transfer buffer: %s", SDL_GetError());
        return false;
    }

    return true;
}

static bool UIRenderer_UploadBuffer(
    EngineContext* ctx,
    SDL_GPUCommandBuffer* cmd,
    SDL_GPUTransferBuffer* transferBuffer,
    SDL_GPUBuffer* gpuBuffer,
    const void* srcData,
    size_t sizeBytes
) {
    void* mapped = SDL_MapGPUTransferBuffer(ctx->gpu, transferBuffer, true);
    if (!mapped) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Failed to map transfer buffer: %s", SDL_GetError());
        return false;
    }

    memcpy(mapped, srcData, sizeBytes);
    SDL_UnmapGPUTransferBuffer(ctx->gpu, transferBuffer);

    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
    if (!copyPass) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Failed to begin copy pass: %s", SDL_GetError());
        return false;
    }

    SDL_GPUTransferBufferLocation src = {
        .transfer_buffer = transferBuffer,
        .offset = 0
    };

    SDL_GPUBufferRegion dst = {
        .buffer = gpuBuffer,
        .offset = 0,
        .size = sizeBytes
    };

    SDL_UploadToGPUBuffer(copyPass, &src, &dst, true);
    SDL_EndGPUCopyPass(copyPass);

    return true;
}

static bool UIRenderer_UploadDirtyBuffers(EngineContext* ctx, SDL_GPUCommandBuffer* cmd) {
    UIRenderer* uiRenderer = &ctx->renderer.uiRenderer;

    if (ctx->ui.panel_dirty) {
        if (!UIRenderer_UploadBuffer(
                ctx,
                cmd,
                uiRenderer->panelTransferBuffer,
                uiRenderer->panelBuffer,
                ctx->ui.panel_meta_buffer,
                GPU_PANEL_META_BUFFER_SIZE
            ))
        {
            return false;
        }

        ctx->ui.panel_dirty = false;
    }

    if (ctx->ui.text_dirty) {
        if (!UIRenderer_UploadBuffer(
                ctx,
                cmd,
                uiRenderer->textTransferBuffer,
                uiRenderer->textBuffer,
                ctx->ui.text_buffer,
                GPU_TEXT_BUFFER_SIZE
            ))
        {
            return false;
        }

        ctx->ui.text_dirty = false;
    }

    if (ctx->ui.text_meta_dirty) {
        if (!UIRenderer_UploadBuffer(
                ctx,
                cmd,
                uiRenderer->textMetaTransferBuffer,
                uiRenderer->textMetaBuffer,
                ctx->ui.text_meta_buffer,
                GPU_TEXT_LINE_META_BUFFER_SIZE
            ))
        {
            return false;
        }

        ctx->ui.text_meta_dirty = false;
    }

    return true;
}

static bool UIRenderer_RenderPanelPass(
    EngineContext* ctx,
    SDL_GPUCommandBuffer* cmd,
    const SDL_GPUStorageTextureReadWriteBinding* outputBinding
) {
    UIRenderer* uiRenderer = &ctx->renderer.uiRenderer;

    SDL_GPUComputePass* panelPass = SDL_BeginGPUComputePass(
        cmd,
        outputBinding,
        1,
        NULL,
        0
    );
    if (!panelPass) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Failed to begin UI panel compute pass: %s", SDL_GetError());
        return false;
    }

    SDL_GPUBuffer* panelStorageBuffers[] = {
        uiRenderer->panelBuffer
    };

    SDL_BindGPUComputePipeline(panelPass, uiRenderer->panelPipeline);
    SDL_BindGPUComputeStorageBuffers(panelPass, 0, panelStorageBuffers, 1);
    SDL_DispatchGPUCompute(panelPass, ctx->renderer.dispatchX, ctx->renderer.dispatchY, 1);
    SDL_EndGPUComputePass(panelPass);

    return true;
}

static bool UIRenderer_RenderTextPass(
    EngineContext* ctx,
    SDL_GPUCommandBuffer* cmd,
    const SDL_GPUStorageTextureReadWriteBinding* outputBinding
) {
    UIRenderer* uiRenderer = &ctx->renderer.uiRenderer;

    SDL_GPUComputePass* textPass = SDL_BeginGPUComputePass(
        cmd,
        outputBinding,
        1,
        NULL,
        0
    );
    if (!textPass) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Failed to begin UI text compute pass: %s", SDL_GetError());
        return false;
    }

    SDL_GPUTextureSamplerBinding fontBinding = {
        .texture = ctx->assets.defaultFont.atlas,
        .sampler = uiRenderer->fontSampler
    };

    SDL_GPUBuffer* textStorageBuffers[] = {
        uiRenderer->textBuffer,
        uiRenderer->textMetaBuffer
    };

    SDL_BindGPUComputePipeline(textPass, uiRenderer->textOverlayPipeline);
    SDL_BindGPUComputeSamplers(textPass, 0, &fontBinding, 1);
    SDL_BindGPUComputeStorageBuffers(textPass, 0, textStorageBuffers, 2);

    uint32_t textPixelsWide = MAX_CHARS_PER_TEXT_LINE * ctx->assets.defaultFont.glyph_w;
    uint32_t textPixelsHigh = MAX_UI_TEXT_LINES * ctx->assets.defaultFont.glyph_h;
    uint32_t textDispatchX = (textPixelsWide + TEXT_WORKGROUP_W - 1) / TEXT_WORKGROUP_W;
    uint32_t textDispatchY = (textPixelsHigh + TEXT_WORKGROUP_H - 1) / TEXT_WORKGROUP_H;

    SDL_DispatchGPUCompute(textPass, textDispatchX, textDispatchY, 1);
    SDL_EndGPUComputePass(textPass);

    return true;
}

bool UIRenderer_Init(EngineContext* ctx) {
    UIRenderer* uiRenderer = &ctx->renderer.uiRenderer;

    uiRenderer->panelPipeline = UIRenderer_CreatePanelPipeline(ctx->gpu);
    if (!uiRenderer->panelPipeline) {
        SDL_LogCritical(SDL_LOG_CATEGORY_RENDER, "UI panel pipeline creation failed: %s", SDL_GetError());
        return false;
    }

    uiRenderer->textOverlayPipeline = UIRenderer_CreateTextOverlayPipeline(ctx->gpu);
    if (!uiRenderer->textOverlayPipeline) {
        SDL_LogCritical(SDL_LOG_CATEGORY_RENDER, "Text overlay pipeline creation failed: %s", SDL_GetError());
        UIRenderer_Shutdown(ctx);
        return false;
    }

    SDL_GPUSamplerCreateInfo samplerInfo = {
        .min_filter = SDL_GPU_FILTER_NEAREST,
        .mag_filter = SDL_GPU_FILTER_NEAREST,
        .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .mip_lod_bias = 0.0f,
        .max_anisotropy = 1.0f,
        .compare_op = SDL_GPU_COMPAREOP_NEVER,
        .min_lod = 0.0f,
        .max_lod = 0.0f,
        .enable_anisotropy = false,
        .enable_compare = false,
        .props = 0
    };

    uiRenderer->fontSampler = SDL_CreateGPUSampler(ctx->gpu, &samplerInfo);
    if (!uiRenderer->fontSampler) {
        SDL_LogCritical(SDL_LOG_CATEGORY_RENDER, "Font sampler creation failed: %s", SDL_GetError());
        UIRenderer_Shutdown(ctx);
        return false;
    }

    if (!UIRenderer_CreateBuffers(ctx)) {
        UIRenderer_Shutdown(ctx);
        return false;
    }

    return true;
}

void UIRenderer_Shutdown(EngineContext* ctx) {
    UIRenderer* uiRenderer = &ctx->renderer.uiRenderer;

    if (uiRenderer->panelPipeline) {
        SDL_ReleaseGPUComputePipeline(ctx->gpu, uiRenderer->panelPipeline);
        uiRenderer->panelPipeline = NULL;
    }
    if (uiRenderer->textOverlayPipeline) {
        SDL_ReleaseGPUComputePipeline(ctx->gpu, uiRenderer->textOverlayPipeline);
        uiRenderer->textOverlayPipeline = NULL;
    }
    if (uiRenderer->fontSampler) {
        SDL_ReleaseGPUSampler(ctx->gpu, uiRenderer->fontSampler);
        uiRenderer->fontSampler = NULL;
    }
    if (uiRenderer->panelBuffer) {
        SDL_ReleaseGPUBuffer(ctx->gpu, uiRenderer->panelBuffer);
        uiRenderer->panelBuffer = NULL;
    }
    if (uiRenderer->textBuffer) {
        SDL_ReleaseGPUBuffer(ctx->gpu, uiRenderer->textBuffer);
        uiRenderer->textBuffer = NULL;
    }
    if (uiRenderer->textMetaBuffer) {
        SDL_ReleaseGPUBuffer(ctx->gpu, uiRenderer->textMetaBuffer);
        uiRenderer->textMetaBuffer = NULL;
    }
    if (uiRenderer->panelTransferBuffer) {
        SDL_ReleaseGPUTransferBuffer(ctx->gpu, uiRenderer->panelTransferBuffer);
        uiRenderer->panelTransferBuffer = NULL;
    }
    if (uiRenderer->textTransferBuffer) {
        SDL_ReleaseGPUTransferBuffer(ctx->gpu, uiRenderer->textTransferBuffer);
        uiRenderer->textTransferBuffer = NULL;
    }
    if (uiRenderer->textMetaTransferBuffer) {
        SDL_ReleaseGPUTransferBuffer(ctx->gpu, uiRenderer->textMetaTransferBuffer);
        uiRenderer->textMetaTransferBuffer = NULL;
    }
}

bool UIRenderer_Render(
    EngineContext* ctx,
    SDL_GPUCommandBuffer* cmd,
    const SDL_GPUStorageTextureReadWriteBinding* outputBinding
) {
    if (!UIRenderer_UploadDirtyBuffers(ctx, cmd)) {
        return false;
    }

    if (!UIRenderer_RenderPanelPass(ctx, cmd, outputBinding)) {
        return false;
    }

    return UIRenderer_RenderTextPass(ctx, cmd, outputBinding);
}
