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
#include <stdio.h>
#include <string.h>
#include "SDL3/SDL_error.h"
#include "SDL3/SDL_gpu.h"
#include "core/types.h"
#include "core/engine.h"
#include "game/matter.h"
#include "render/ui_renderer.h"

#define BASE_SHORT_SIDE 256
#define CAMERA_FOLLOW_RATE 5.5f
#define GPU_MATTER_NODE_BUFFER_SIZE (MAX_MATTER_NODES * sizeof(MatterNodeGPU))
#define PHYSICS_DEBUG_CIRCLE_WIDTH 0.65f
#define PHYSICS_DEBUG_FIELD_WIDTH 0.045f

// -----------------------------------------------------------------------------
// Internal Helpers
// -----------------------------------------------------------------------------

// Reads raw binary assets (SPIR-V / Metallib) from disk.
static void* LoadFile(const char* path, size_t* outSize) {
    SDL_IOStream* io = SDL_IOFromFile(path, "rb");
    if (!io) {
        SDL_Log("RENDER: Failed to open shader: %s", path);
        return NULL;
    }

    size_t size = SDL_GetIOSize(io);
    void* data = SDL_malloc(size);

    if (SDL_ReadIO(io, data, size) != size) {
        SDL_Log("RENDER: Short read on shader: %s", path);
        SDL_free(data);
        SDL_CloseIO(io);
        return NULL;
    }

    SDL_CloseIO(io);
    if (outSize) *outSize = size;
    return data;
}

// Returns "metallib" (Apple) or "spv" (Vulkan/Generic) for file paths.
static const char* GetShaderExtension(void) {
    #if defined(SDL_PLATFORM_MACOS) || defined(SDL_PLATFORM_IOS)
        return "metallib";
    #else
        return "spv";
    #endif
}

// Pipeline initializer.
static SDL_GPUComputePipeline* _CreateComputePipeline(SDL_GPUDevice* gpu) {
    // Shader Path Resolution
    char shaderPath[256];
    const char* ext = GetShaderExtension();
    const char* basePath = SDL_GetBasePath();

    if (basePath) {
        // PC/Mac/iOS: Assets are in a specific Resources/bin folder
        snprintf(shaderPath, sizeof(shaderPath), "%sassets/shaders/BasicCompute.%s", basePath, ext);
    } else {
        // Android: Assets are relative to the APK root
        snprintf(shaderPath, sizeof(shaderPath), "shaders/BasicCompute.%s", ext);
    }

    // Pipeline Creation
    size_t codeSize;
    void* code = LoadFile(shaderPath, &codeSize);
    if (!code) return NULL;

    // Metal (via SPIRV-Cross) renames main -> main0
    const char* entryPoint = (Engine_GetShaderFormat() == SDL_GPU_SHADERFORMAT_METALLIB) ? "main0" : "main";

    SDL_GPUComputePipelineCreateInfo pipelineInfo = {
        .code = code,
        .code_size = codeSize,
        .entrypoint = entryPoint,
        .format = Engine_GetShaderFormat(),

        .num_samplers = 0,
        .num_readonly_storage_textures = 0,
        .num_readonly_storage_buffers = 1,
        .num_readwrite_storage_textures = 1, // Binding 0
        .num_readwrite_storage_buffers = 0,
        .num_uniform_buffers = 1,
        .threadcount_x = 8,
        .threadcount_y = 8,
        .threadcount_z = 1,
        .props = 0
    };

    SDL_GPUComputePipeline* pipeline = SDL_CreateGPUComputePipeline(gpu, &pipelineInfo);
    SDL_free(code);

    return pipeline;
}

static bool Renderer_CreateMatterBuffers(EngineContext* ctx) {
    SDL_GPUBufferCreateInfo nodeBufferInfo = {
        .usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ,
        .size = GPU_MATTER_NODE_BUFFER_SIZE,
        .props = 0
    };

    ctx->renderer.matterNodeBuffer = SDL_CreateGPUBuffer(ctx->gpu, &nodeBufferInfo);
    if (!ctx->renderer.matterNodeBuffer) {
        SDL_LogCritical(SDL_LOG_CATEGORY_RENDER, "Failed to create matter node buffer: %s", SDL_GetError());
        return false;
    }

    SDL_GPUTransferBufferCreateInfo nodeTransferInfo = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = GPU_MATTER_NODE_BUFFER_SIZE,
        .props = 0
    };

    ctx->renderer.matterNodeTransferBuffer = SDL_CreateGPUTransferBuffer(ctx->gpu, &nodeTransferInfo);
    if (!ctx->renderer.matterNodeTransferBuffer) {
        SDL_LogCritical(SDL_LOG_CATEGORY_RENDER, "Failed to create matter node transfer buffer: %s", SDL_GetError());
        return false;
    }

    return true;
}

static bool Renderer_UploadBuffer(
    EngineContext* ctx,
    SDL_GPUCommandBuffer* cmd,
    SDL_GPUTransferBuffer* transferBuffer,
    SDL_GPUBuffer* gpuBuffer,
    const void* srcData,
    size_t sizeBytes
) {
    void* mapped = SDL_MapGPUTransferBuffer(ctx->gpu, transferBuffer, true);
    if (!mapped) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Failed to map renderer transfer buffer: %s", SDL_GetError());
        return false;
    }

    memcpy(mapped, srcData, sizeBytes);
    SDL_UnmapGPUTransferBuffer(ctx->gpu, transferBuffer);

    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
    if (!copyPass) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Failed to begin renderer copy pass: %s", SDL_GetError());
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

static Vec2 Renderer_ViewOrigin(const Renderer* renderer) {
    if (!renderer || !renderer->cameraInitialized) {
        return (Vec2){0.0f, 0.0f};
    }

    return (Vec2){
        renderer->cameraCenter.x - (float)renderer->internalW * 0.5f,
        renderer->cameraCenter.y - (float)renderer->internalH * 0.5f
    };
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

bool Renderer_Init(EngineContext* ctx) {
    ctx->renderer.physicsDebugFlags = RENDERER_PHYSICS_DEBUG_DEFAULT;

    // Create pipelines
    ctx->renderer.computePipeline = _CreateComputePipeline(ctx->gpu);
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

        ShaderUniforms uniforms = {
            .resolution = {
                (float)ctx->renderer.internalW,
                (float)ctx->renderer.internalH
            },
            .matterNodeCount = ctx->matter.node_count,
            .matterThreshold = MATTER_FIELD_THRESHOLD,
            .viewOrigin = Renderer_ViewOrigin(&ctx->renderer),
            .cursorPos = cursorPos,
            .cursorVisible = cursorVisible ? 1u : 0u,
            .physicsDebugFlags = ctx->renderer.physicsDebugFlags,
            .physicsDebugCircleWidth = PHYSICS_DEBUG_CIRCLE_WIDTH,
            .physicsDebugFieldWidth = PHYSICS_DEBUG_FIELD_WIDTH
        };

        if (ctx->matter.dirty) {
            if (!Renderer_UploadBuffer(
                    ctx,
                    cmd,
                    ctx->renderer.matterNodeTransferBuffer,
                    ctx->renderer.matterNodeBuffer,
                    ctx->matter.gpu_nodes,
                    GPU_MATTER_NODE_BUFFER_SIZE
                ))
            {
                SDL_CancelGPUCommandBuffer(cmd);
                return false;
            }

            ctx->matter.dirty = false;
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
