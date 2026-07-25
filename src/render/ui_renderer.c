#include "ui_renderer.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_log.h>
#include <stdbool.h>

#include "core/engine.h"
#include "render/gpu_common.h"
#include "ui/ui_internal.h"

#define UI_WORKGROUP_W 8
#define UI_WORKGROUP_H 8
#define GPU_TEXT_BUFFER_SIZE (MAX_UI_TEXT_LINES * MAX_CHARS_PER_TEXT_LINE * sizeof(uint32_t))
#define GPU_TEXT_LINE_META_BUFFER_SIZE (MAX_UI_TEXT_LINES * UITEXTLINE_META_STRIDE * sizeof(uint32_t))
#define GPU_PANEL_META_BUFFER_SIZE (MAX_UI_PANELS * UIPANEL_META_STRIDE * sizeof(uint32_t))

typedef struct UITextDispatchBounds {
    uint32_t width;
    uint32_t height;
} UITextDispatchBounds;

typedef struct UIPanelDispatchBounds {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} UIPanelDispatchBounds;

typedef struct UIPanelUniforms {
    uint32_t origin_x;
    uint32_t origin_y;
    uint32_t width;
    uint32_t height;
} UIPanelUniforms;

static UITextDispatchBounds UIRenderer_TextDispatchBounds(EngineContext* ctx) {
    UITextDispatchBounds bounds = {0};

    for (uint32_t i = 0; i < MAX_UI_TEXT_LINES; i++) {
        const UITextLine* line = &ctx->ui.text_lines[i];
        if (!line->active || line->length == 0) {
            continue;
        }

        uint32_t width = (uint32_t)line->length * ctx->assets.defaultFont.glyph_w;
        uint32_t height = (i + 1u) * ctx->assets.defaultFont.glyph_h;
        if (width > bounds.width) {
            bounds.width = width;
        }
        if (height > bounds.height) {
            bounds.height = height;
        }
    }

    return bounds;
}

static uint32_t UIRenderer_ColorAlpha(uint32_t rgba) {
    return rgba & 0xffu;
}

static bool UIRenderer_PanelVisible(const UIPanel* panel) {
    if (!panel->active) {
        return false;
    }

    if (UIRenderer_ColorAlpha(panel->style.fill_color) > 0u) {
        return true;
    }

    return panel->style.border > 0 &&
        UIRenderer_ColorAlpha(panel->style.border_color) > 0u;
}

static UIPanelDispatchBounds UIRenderer_PanelDispatchBounds(EngineContext* ctx) {
    UIPanelDispatchBounds bounds = {0};
    int min_x = (int)ctx->renderer.internalW;
    int min_y = (int)ctx->renderer.internalH;
    int max_x = 0;
    int max_y = 0;

    for (uint32_t i = 0; i < MAX_UI_PANELS; i++) {
        const UIPanel* panel = &ctx->ui.panels[i];
        if (!UIRenderer_PanelVisible(panel) || panel->width == 0 || panel->height == 0) {
            continue;
        }

        int panel_min_x = panel->x;
        int panel_min_y = panel->y;
        int panel_max_x = panel->x + (int)panel->width;
        int panel_max_y = panel->y + (int)panel->height;

        if (panel_max_x <= 0 ||
            panel_max_y <= 0 ||
            panel_min_x >= (int)ctx->renderer.internalW ||
            panel_min_y >= (int)ctx->renderer.internalH)
        {
            continue;
        }

        if (panel_min_x < 0) panel_min_x = 0;
        if (panel_min_y < 0) panel_min_y = 0;
        if (panel_max_x > (int)ctx->renderer.internalW) panel_max_x = (int)ctx->renderer.internalW;
        if (panel_max_y > (int)ctx->renderer.internalH) panel_max_y = (int)ctx->renderer.internalH;

        if (panel_min_x < min_x) min_x = panel_min_x;
        if (panel_min_y < min_y) min_y = panel_min_y;
        if (panel_max_x > max_x) max_x = panel_max_x;
        if (panel_max_y > max_y) max_y = panel_max_y;
    }

    if (max_x <= min_x || max_y <= min_y) {
        return bounds;
    }

    bounds.x = (uint32_t)min_x;
    bounds.y = (uint32_t)min_y;
    bounds.width = (uint32_t)(max_x - min_x);
    bounds.height = (uint32_t)(max_y - min_y);
    return bounds;
}

static SDL_GPUComputePipeline* UIRenderer_CreateTextOverlayPipeline(SDL_GPUDevice* gpu) {
    RenderGPUComputePipelineInfo pipeline_info = {
        .sampler_count = 1,
        .storage_buffer_count = 2,
        .uniform_buffer_count = 0,
        .thread_count_x = 8,
        .thread_count_y = 8,
        .thread_count_z = 1
    };

    return RenderGPU_CreateComputePipeline(gpu, "TextOverlayCompute", &pipeline_info);
}

static SDL_GPUComputePipeline* UIRenderer_CreatePanelPipeline(SDL_GPUDevice* gpu) {
    RenderGPUComputePipelineInfo pipeline_info = {
        .sampler_count = 0,
        .storage_buffer_count = 1,
        .uniform_buffer_count = 1,
        .thread_count_x = 8,
        .thread_count_y = 8,
        .thread_count_z = 1
    };

    return RenderGPU_CreateComputePipeline(gpu, "UIPanelCompute", &pipeline_info);
}

static bool UIRenderer_CreateBuffers(EngineContext* ctx) {
    UIRenderer* uiRenderer = &ctx->renderer.uiRenderer;

    uiRenderer->panelBuffer = RenderGPU_CreateStorageBuffer(
        ctx->gpu,
        GPU_PANEL_META_BUFFER_SIZE,
        "UI panel"
    );
    if (!uiRenderer->panelBuffer) {
        return false;
    }

    uiRenderer->textBuffer = RenderGPU_CreateStorageBuffer(
        ctx->gpu,
        GPU_TEXT_BUFFER_SIZE,
        "UI text"
    );
    if (!uiRenderer->textBuffer) {
        return false;
    }

    uiRenderer->textMetaBuffer = RenderGPU_CreateStorageBuffer(
        ctx->gpu,
        GPU_TEXT_LINE_META_BUFFER_SIZE,
        "UI text line meta"
    );
    if (!uiRenderer->textMetaBuffer) {
        return false;
    }

    uiRenderer->panelTransferBuffer = RenderGPU_CreateUploadTransferBuffer(
        ctx->gpu,
        GPU_PANEL_META_BUFFER_SIZE,
        "UI panel"
    );
    if (!uiRenderer->panelTransferBuffer) {
        return false;
    }

    uiRenderer->textTransferBuffer = RenderGPU_CreateUploadTransferBuffer(
        ctx->gpu,
        GPU_TEXT_BUFFER_SIZE,
        "UI text"
    );
    if (!uiRenderer->textTransferBuffer) {
        return false;
    }

    uiRenderer->textMetaTransferBuffer = RenderGPU_CreateUploadTransferBuffer(
        ctx->gpu,
        GPU_TEXT_LINE_META_BUFFER_SIZE,
        "UI text line meta"
    );
    if (!uiRenderer->textMetaTransferBuffer) {
        return false;
    }

    return true;
}

static bool UIRenderer_UploadDirtyBuffers(EngineContext* ctx, SDL_GPUCommandBuffer* cmd) {
    UIRenderer* uiRenderer = &ctx->renderer.uiRenderer;

    if (ctx->ui.panel_dirty) {
        if (!RenderGPU_UploadBuffer(
                ctx->gpu,
                cmd,
                uiRenderer->panelTransferBuffer,
                uiRenderer->panelBuffer,
                ctx->ui.panel_meta_buffer,
                GPU_PANEL_META_BUFFER_SIZE,
                "UI panel"
            ))
        {
            return false;
        }

        ctx->ui.panel_dirty = false;
    }

    if (ctx->ui.text_dirty) {
        if (!RenderGPU_UploadBuffer(
                ctx->gpu,
                cmd,
                uiRenderer->textTransferBuffer,
                uiRenderer->textBuffer,
                ctx->ui.text_buffer,
                GPU_TEXT_BUFFER_SIZE,
                "UI text"
            ))
        {
            return false;
        }

        ctx->ui.text_dirty = false;
    }

    if (ctx->ui.text_meta_dirty) {
        if (!RenderGPU_UploadBuffer(
                ctx->gpu,
                cmd,
                uiRenderer->textMetaTransferBuffer,
                uiRenderer->textMetaBuffer,
                ctx->ui.text_meta_buffer,
                GPU_TEXT_LINE_META_BUFFER_SIZE,
                "UI text meta"
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
    UIPanelDispatchBounds bounds = UIRenderer_PanelDispatchBounds(ctx);
    if (bounds.width == 0 || bounds.height == 0) {
        return true;
    }

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

    UIPanelUniforms uniforms = {
        .origin_x = bounds.x,
        .origin_y = bounds.y,
        .width = bounds.width,
        .height = bounds.height
    };
    SDL_PushGPUComputeUniformData(cmd, 0, &uniforms, sizeof(uniforms));

    uint32_t dispatchX = (bounds.width + UI_WORKGROUP_W - 1) / UI_WORKGROUP_W;
    uint32_t dispatchY = (bounds.height + UI_WORKGROUP_H - 1) / UI_WORKGROUP_H;
    SDL_DispatchGPUCompute(panelPass, dispatchX, dispatchY, 1);
    SDL_EndGPUComputePass(panelPass);

    return true;
}

static bool UIRenderer_RenderTextPass(
    EngineContext* ctx,
    SDL_GPUCommandBuffer* cmd,
    const SDL_GPUStorageTextureReadWriteBinding* outputBinding
) {
    UITextDispatchBounds bounds = UIRenderer_TextDispatchBounds(ctx);
    if (bounds.width == 0 || bounds.height == 0) {
        return true;
    }

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

    uint32_t textDispatchX = (bounds.width + UI_WORKGROUP_W - 1) / UI_WORKGROUP_W;
    uint32_t textDispatchY = (bounds.height + UI_WORKGROUP_H - 1) / UI_WORKGROUP_H;

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
