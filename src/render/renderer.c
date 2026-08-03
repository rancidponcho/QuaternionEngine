/*
================================================================================
    Renderer
    Handles the high-level graphics pipeline:
    - Shader loading & compilation
    - Resolution scaling
    - Compute Dispatch & Blitting
================================================================================
*/

#include "renderer.h"

#include <math.h>
#include <SDL3/SDL_log.h>
#include <stdbool.h>
#include <stddef.h>
#include "SDL3/SDL_error.h"
#include "SDL3/SDL_gpu.h"
#include "core/types.h"
#include "core/engine.h"
#include "game/matter.h"
#include "render/gpu_common.h"
#include "render/ui_renderer.h"

#define BASE_SHORT_SIDE 256
#define CAMERA_FOLLOW_RATE 5.5f
#define GPU_MATTER_NODE_BUFFER_SIZE (MAX_MATTER_NODES * sizeof(MatterNodeGPU))
#define MATTER_RENDER_FIELD_SUPPORT_SCALE 2.25f
#define PHYSICS_DEBUG_CIRCLE_WIDTH 0.65f
#define PHYSICS_DEBUG_FIELD_WIDTH 0.045f
#define MINING_BEAM_WIDTH 0.9f
#define MINING_TIP_RADIUS 5.0f
#define MINING_TOOL_WIDTH 1.6f
#define MINING_TOOL_TIP_RADIUS 2.4f

// -----------------------------------------------------------------------------
// Internal Helpers
// -----------------------------------------------------------------------------

// Pipeline initializer.
static SDL_GPUComputePipeline* Renderer_CreateComputePipeline(SDL_GPUDevice* gpu) {
    RenderGPUComputePipelineInfo pipeline_info = {
        .sampler_count = 0,
        .storage_buffer_count = 1,
        .uniform_buffer_count = 1,
        .thread_count_x = 8,
        .thread_count_y = 8,
        .thread_count_z = 1
    };

    return RenderGPU_CreateComputePipeline(gpu, "BasicCompute", &pipeline_info);
}

static bool Renderer_CreateMatterBuffers(EngineContext* ctx) {
    ctx->renderer.matterNodeBuffer = RenderGPU_CreateStorageBuffer(
        ctx->gpu,
        GPU_MATTER_NODE_BUFFER_SIZE,
        "matter node"
    );
    if (!ctx->renderer.matterNodeBuffer) {
        return false;
    }

    ctx->renderer.matterNodeTransferBuffer = RenderGPU_CreateUploadTransferBuffer(
        ctx->gpu,
        GPU_MATTER_NODE_BUFFER_SIZE,
        "matter node"
    );
    if (!ctx->renderer.matterNodeTransferBuffer) {
        return false;
    }

    return true;
}

static Vec2 Renderer_ViewOrigin(const Renderer* renderer) {
    if (!renderer || !renderer->cameraInitialized) {
        return (Vec2){0.0f, 0.0f};
    }

    return (Vec2){
        renderer->cameraCenter.x - (float)renderer->internalW * 0.5f,
        renderer->cameraCenter.y - (float)renderer->internalH * 0.5f
    };
}

static bool Renderer_MatterNodeTouchesView(
    const MatterNodeGPU* node,
    Vec2 view_origin,
    uint32_t view_w,
    uint32_t view_h
) {
    float margin = node->radius * MATTER_RENDER_FIELD_SUPPORT_SCALE;
    float right = view_origin.x + (float)view_w;
    float bottom = view_origin.y + (float)view_h;

    return node->pos.x + margin >= view_origin.x &&
        node->pos.y + margin >= view_origin.y &&
        node->pos.x - margin <= right &&
        node->pos.y - margin <= bottom;
}

static uint32_t Renderer_CollectVisibleMatterNodes(
    EngineContext* ctx,
    Vec2 view_origin,
    MatterNodeGPU* out_nodes
) {
    uint32_t count = 0;
    const MatterNodeGPU* matter_nodes = NULL;
    uint32_t matter_node_count = MatterWorld_GetGPUNodes(&ctx->matter, &matter_nodes);

    for (uint32_t i = 0; i < matter_node_count; i++) {
        const MatterNodeGPU* node = &matter_nodes[i];
        if (!Renderer_MatterNodeTouchesView(
                node,
                view_origin,
                ctx->renderer.internalW,
                ctx->renderer.internalH
            ))
        {
            continue;
        }

        out_nodes[count++] = *node;
    }

    return count;
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

bool Renderer_Init(EngineContext* ctx) {
    ctx->renderer.physicsDebugFlags = RENDERER_PHYSICS_DEBUG_DEFAULT;

    // Create pipelines
    ctx->renderer.computePipeline = Renderer_CreateComputePipeline(ctx->gpu);
    if (!ctx->renderer.computePipeline) {
        SDL_LogCritical(SDL_LOG_CATEGORY_RENDER, "Pipeline Creation Failed: %s", SDL_GetError());
        return false;
    }

    if (!Renderer_CreateMatterBuffers(ctx)) {
        Renderer_Shutdown(ctx);
        return false;
    }

    if (!UIRenderer_Init(ctx)) {
        Renderer_Shutdown(ctx);
        return false;
    }

    // Initial Resize
    int w, h;
    SDL_GetWindowSizeInPixels(ctx->window, &w, &h);
    Renderer_Resize(ctx, w, h);

    return true;
}

void Renderer_Shutdown(EngineContext* ctx) {
    if (!ctx || !ctx->gpu) {
        return;
    }

    SDL_WaitForGPUIdle(ctx->gpu);

    if (ctx->renderer.computePipeline) {
        SDL_ReleaseGPUComputePipeline(ctx->gpu, ctx->renderer.computePipeline);
        ctx->renderer.computePipeline = NULL;
    }
    UIRenderer_Shutdown(ctx);

    if (ctx->renderer.matterNodeBuffer) {
        SDL_ReleaseGPUBuffer(ctx->gpu, ctx->renderer.matterNodeBuffer);
        ctx->renderer.matterNodeBuffer = NULL;
    }
    if (ctx->renderer.matterNodeTransferBuffer) {
        SDL_ReleaseGPUTransferBuffer(ctx->gpu, ctx->renderer.matterNodeTransferBuffer);
        ctx->renderer.matterNodeTransferBuffer = NULL;
    }
    if (ctx->renderer.drawTexture) {
        SDL_ReleaseGPUTexture(ctx->gpu, ctx->renderer.drawTexture);
        ctx->renderer.drawTexture = NULL;
    }
}

void Renderer_Resize(EngineContext* ctx, int winW, int winH) {
    if (winW <= 0 || winH <= 0) return;

    ctx->renderer.outputW = winW;
    ctx->renderer.outputH = winH;

    // -------------------------------------------------------------------------
    // Aspect Ratio Logic (Pixel Art Scaling)
    // -------------------------------------------------------------------------
    // Lock the shortest side, calculate integer scale.
    int minDim = (winW < winH) ? winW : winH;
    int scale = minDim / BASE_SHORT_SIDE;
    if (scale < 1) scale = 1;

    int newTexW = winW / scale;
    int newTexH = winH / scale;

    // -------------------------------------------------------------------------
    // Reallocation
    // -------------------------------------------------------------------------
    if (ctx->renderer.internalW != newTexW || ctx->renderer.internalH != newTexH) {
        SDL_WaitForGPUIdle(ctx->gpu);

        if (ctx->renderer.drawTexture) {
            SDL_ReleaseGPUTexture(ctx->gpu, ctx->renderer.drawTexture);
        }

        ctx->renderer.internalW = newTexW;
        ctx->renderer.internalH = newTexH;

        SDL_GPUTextureCreateInfo tex_info = {
            .type = SDL_GPU_TEXTURETYPE_2D,
            .format = SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT,
            .width = ctx->renderer.internalW,
            .height = ctx->renderer.internalH,
            .layer_count_or_depth = 1,
            .num_levels = 1,
            .sample_count = SDL_GPU_SAMPLECOUNT_1,
            .usage = SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_TEXTUREUSAGE_SAMPLER,
            .props = 0
        };

        ctx->renderer.drawTexture = SDL_CreateGPUTexture(ctx->gpu, &tex_info);

        if (!ctx->renderer.drawTexture) {
            SDL_LogCritical(SDL_LOG_CATEGORY_RENDER, "Failed to resize VRAM texture to %dx%d", newTexW, newTexH);
        } else {
            SDL_Log("System: VRAM Resized [%dx%d]", newTexW, newTexH);
        }

        // Update simulation dispatch groups to match new texture size
        ctx->renderer.dispatchX = (newTexW + 7) / 8;
        ctx->renderer.dispatchY = (newTexH + 7) / 8;

        if (!ctx->renderer.cameraInitialized) {
            Renderer_SetCameraCenter(
                ctx,
                (Vec2){
                    (float)ctx->renderer.internalW * 0.5f,
                    (float)ctx->renderer.internalH * 0.5f
                }
            );
        }
    }

    // Viewport centering
    int finalW = ctx->renderer.internalW * scale;
    int finalH = ctx->renderer.internalH * scale;

    ctx->renderer.viewport.x = (winW - finalW) / 2;
    ctx->renderer.viewport.y = (winH - finalH) / 2;
    ctx->renderer.viewport.w = finalW;
    ctx->renderer.viewport.h = finalH;
}

void Renderer_SetCameraCenter(EngineContext* ctx, Vec2 center) {
    if (!ctx) {
        return;
    }

    ctx->renderer.cameraCenter = center;
    ctx->renderer.cameraInitialized = true;
}

void Renderer_UpdateCamera(EngineContext* ctx, Vec2 target_center, float dt) {
    if (!ctx) {
        return;
    }

    if (!ctx->renderer.cameraInitialized || dt <= 0.0f) {
        Renderer_SetCameraCenter(ctx, target_center);
        return;
    }

    float t = 1.0f - expf(-CAMERA_FOLLOW_RATE * dt);
    Vec2 delta = Vec2_Sub(target_center, ctx->renderer.cameraCenter);
    ctx->renderer.cameraCenter = Vec2_Add(
        ctx->renderer.cameraCenter,
        Vec2_Scale(delta, t)
    );
}

Vec2 Renderer_GetInternalSize(const EngineContext* ctx) {
    if (!ctx) {
        return (Vec2){0.0f, 0.0f};
    }

    return (Vec2){
        (float)ctx->renderer.internalW,
        (float)ctx->renderer.internalH
    };
}

void Renderer_TogglePhysicsDebug(EngineContext* ctx, Uint32 flags) {
    if (!ctx) {
        return;
    }

    ctx->renderer.physicsDebugFlags ^= flags;
}

Uint32 Renderer_GetVisibleMatterNodeCount(const EngineContext* ctx) {
    return ctx ? ctx->renderer.visibleMatterNodeCount : 0u;
}

void Renderer_SetMiningTool(EngineContext* ctx, Vec2 start, Vec2 end, bool active, bool firing) {
    if (!ctx) {
        return;
    }

    ctx->renderer.miningToolStart = start;
    ctx->renderer.miningToolEnd = end;
    ctx->renderer.miningToolActive = active;
    ctx->renderer.miningToolFiring = firing;
}

void Renderer_SetMiningBeam(EngineContext* ctx, Vec2 start, Vec2 end, bool active, bool hit) {
    if (!ctx) {
        return;
    }

    ctx->renderer.miningBeamStart = start;
    ctx->renderer.miningBeamEnd = end;
    ctx->renderer.miningBeamActive = active;
    ctx->renderer.miningBeamHit = hit;
}

void Renderer_ClearMiningOverlay(EngineContext* ctx) {
    Vec2 zero = {0.0f, 0.0f};
    Renderer_SetMiningTool(ctx, zero, zero, false, false);
    Renderer_SetMiningBeam(ctx, zero, zero, false, false);
}

bool Renderer_WindowToInternalPoint(const EngineContext* ctx, Vec2 window_point, Vec2* internal_point) {
    if (!ctx || !internal_point) {
        return false;
    }

    SDL_Rect viewport = ctx->renderer.viewport;
    if (viewport.w <= 0 || viewport.h <= 0) {
        return false;
    }

    if (window_point.x < (float)viewport.x ||
        window_point.y < (float)viewport.y ||
        window_point.x >= (float)(viewport.x + viewport.w) ||
        window_point.y >= (float)(viewport.y + viewport.h))
    {
        return false;
    }

    internal_point->x = (window_point.x - (float)viewport.x) *
        ((float)ctx->renderer.internalW / (float)viewport.w);
    internal_point->y = (window_point.y - (float)viewport.y) *
        ((float)ctx->renderer.internalH / (float)viewport.h);

    return true;
}

bool Renderer_WindowToWorldPoint(const EngineContext* ctx, Vec2 window_point, Vec2* world_point) {
    if (!world_point) {
        return false;
    }

    Vec2 internal_point;
    if (!Renderer_WindowToInternalPoint(ctx, window_point, &internal_point)) {
        return false;
    }

    *world_point = Vec2_Add(internal_point, Renderer_ViewOrigin(&ctx->renderer));
    return true;
}

bool Renderer_Render(EngineContext* ctx) {
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(ctx->gpu);
    if (!cmd) return false;

    SDL_GPUTexture* swapchainTex;
    Uint32 w, h;

    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, ctx->window, &swapchainTex, &w, &h)) {
        SDL_CancelGPUCommandBuffer(cmd);
        SDL_Delay(100);
        return false;
    }

    if (swapchainTex) {
        SDL_GPUStorageTextureReadWriteBinding storageBinding = {
            .texture = ctx->renderer.drawTexture,
            .mip_level = 0,
            .layer = 0,
            .cycle = false
        };

        Vec2 cursorPos = {0.0f, 0.0f};
        bool cursorVisible = Renderer_WindowToInternalPoint(ctx, ctx->input.mousePos, &cursorPos);

        Vec2 viewOrigin = Renderer_ViewOrigin(&ctx->renderer);
        MatterNodeGPU visibleMatterNodes[MAX_MATTER_NODES];
        uint32_t visibleMatterNodeCount = Renderer_CollectVisibleMatterNodes(
            ctx,
            viewOrigin,
            visibleMatterNodes
        );
        ctx->renderer.visibleMatterNodeCount = visibleMatterNodeCount;

        ShaderUniforms uniforms = {
            .resolution = {
                (float)ctx->renderer.internalW,
                (float)ctx->renderer.internalH
            },
            .matterNodeCount = visibleMatterNodeCount,
            .matterThreshold = MATTER_FIELD_THRESHOLD,
            .viewOrigin = viewOrigin,
            .cursorPos = cursorPos,
            .cursorVisible = cursorVisible ? 1u : 0u,
            .physicsDebugFlags = ctx->renderer.physicsDebugFlags,
            .physicsDebugCircleWidth = PHYSICS_DEBUG_CIRCLE_WIDTH,
            .physicsDebugFieldWidth = PHYSICS_DEBUG_FIELD_WIDTH,
            .miningBeamStart = ctx->renderer.miningBeamStart,
            .miningBeamEnd = ctx->renderer.miningBeamEnd,
            .miningBeamActive = ctx->renderer.miningBeamActive ? 1u : 0u,
            .miningBeamHit = ctx->renderer.miningBeamHit ? 1u : 0u,
            .miningBeamWidth = MINING_BEAM_WIDTH,
            .miningTipRadius = MINING_TIP_RADIUS,
            .miningToolStart = ctx->renderer.miningToolStart,
            .miningToolEnd = ctx->renderer.miningToolEnd,
            .miningToolActive = ctx->renderer.miningToolActive ? 1u : 0u,
            .miningToolFiring = ctx->renderer.miningToolFiring ? 1u : 0u,
            .miningToolWidth = MINING_TOOL_WIDTH,
            .miningToolTipRadius = MINING_TOOL_TIP_RADIUS
        };

        size_t matterUploadSize = visibleMatterNodeCount * sizeof(MatterNodeGPU);
        if (matterUploadSize > 0 &&
            !RenderGPU_UploadBuffer(
                ctx->gpu,
                cmd,
                ctx->renderer.matterNodeTransferBuffer,
                ctx->renderer.matterNodeBuffer,
                visibleMatterNodes,
                matterUploadSize,
                "matter node"
            ))
        {
            SDL_CancelGPUCommandBuffer(cmd);
            return false;
        }
        // ---------------------------------------------------------------------
        // Base Compute Pass
        SDL_GPUComputePass* computePass = SDL_BeginGPUComputePass(
            cmd,
            &storageBinding,
            1,
            NULL,
            0
        );
        if (!computePass) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Failed to begin scene compute pass: %s", SDL_GetError());
            SDL_CancelGPUCommandBuffer(cmd);
            return false;
        }

        SDL_BindGPUComputePipeline(computePass, ctx->renderer.computePipeline);
        SDL_GPUBuffer* matterStorageBuffers[] = {ctx->renderer.matterNodeBuffer};
        SDL_BindGPUComputeStorageBuffers(computePass, 0, matterStorageBuffers, 1);
        SDL_PushGPUComputeUniformData(cmd, 0, &uniforms, sizeof(uniforms));
        SDL_DispatchGPUCompute(computePass, ctx->renderer.dispatchX, ctx->renderer.dispatchY, 1);
        SDL_EndGPUComputePass(computePass);

        if (!UIRenderer_Render(ctx, cmd, &storageBinding)) {
            SDL_CancelGPUCommandBuffer(cmd);
            return false;
        }

        // ---------------------------------------------------------------------
        // Blit Pass (Scale Internal -> Window)
        // ---------------------------------------------------------------------
        SDL_GPUBlitInfo blitInfo = {
            .source.texture = ctx->renderer.drawTexture,
            .source.w = ctx->renderer.internalW,
            .source.h = ctx->renderer.internalH,

            .destination.texture = swapchainTex,
            .destination.x = ctx->renderer.viewport.x,
            .destination.y = ctx->renderer.viewport.y,
            .destination.w = ctx->renderer.viewport.w,
            .destination.h = ctx->renderer.viewport.h,

            .load_op = SDL_GPU_LOADOP_CLEAR,
            .clear_color = {0, 0, 0, 1}, // Letterbox color
            .filter = SDL_GPU_FILTER_NEAREST
        };

        SDL_BlitGPUTexture(cmd, &blitInfo);
    }

    if (!SDL_SubmitGPUCommandBuffer(cmd)) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Failed to submit renderer command buffer: %s", SDL_GetError());
        return false;
    }

    return true;
}
