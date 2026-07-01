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

#include <SDL3/SDL_log.h>
#include <stdbool.h>
#include <stdio.h>
#include "SDL3/SDL_error.h"
#include "SDL3/SDL_gpu.h"
#include "core/types.h"
#include "core/engine.h"
#include "render/ui_renderer.h"

#define BASE_SHORT_SIDE 360

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

// Pipeline ititializer (Also used for hotloading)
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

        .num_readonly_storage_textures = 0,
        .num_uniform_buffers = 1,
        .num_readwrite_storage_textures = 1, // Binding 0
        .threadcount_x = 8,
        .threadcount_y = 8,
        .threadcount_z = 1
    };

    SDL_GPUComputePipeline* pipeline = SDL_CreateGPUComputePipeline(gpu, &pipelineInfo);
    SDL_free(code);
    
    return pipeline;
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

bool Renderer_Init(EngineContext* ctx) {
    // Create pipelines
    ctx->renderer.computePipeline = _CreateComputePipeline(ctx->gpu);
    if (!ctx->renderer.computePipeline) {
        SDL_LogCritical(SDL_LOG_CATEGORY_RENDER, "Pipeline Creation Failed: %s", SDL_GetError());
        return false;
    }

    if (!UIRenderer_Init(ctx)) {
        return false;
    }

    // Initial Resize
    int w, h;
    SDL_GetWindowSizeInPixels(ctx->window, &w, &h);
    Renderer_Resize(ctx, w, h);

    return true;
}

void Renderer_Shutdown(EngineContext* ctx) {
    SDL_WaitForGPUIdle(ctx->gpu);

    if (ctx->renderer.computePipeline) {
        SDL_ReleaseGPUComputePipeline(ctx->gpu, ctx->renderer.computePipeline);
        ctx->renderer.computePipeline = NULL;
    }
    UIRenderer_Shutdown(ctx);

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
       // Engine_ResizeTexture(ctx, newTexW, newTexH);
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
    }

    // Viewport centering
    int finalW = ctx->renderer.internalW * scale;
    int finalH = ctx->renderer.internalH * scale;
    
    ctx->renderer.viewport.x = (winW - finalW) / 2;
    ctx->renderer.viewport.y = (winH - finalH) / 2;
    ctx->renderer.viewport.w = finalW;
    ctx->renderer.viewport.h = finalH;
}

bool Renderer_Render(EngineContext* ctx) {
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(ctx->gpu);
    if (!cmd) return false; 

    SDL_GPUTexture* swapchainTex;
    Uint32 w, h;

    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, ctx->window, &swapchainTex, &w, &h)) {
        SDL_CancelGPUCommandBuffer(cmd);
        SDL_Delay(100); // Safety valve?
        return false;
    }

    if (swapchainTex) {
        SDL_GPUStorageTextureReadWriteBinding storageBinding = {
            .texture = ctx->renderer.drawTexture, 
            .mip_level = 0, 
            .layer = 0, 
            .cycle = false
        };

        // ---------------------------------------------------------------------
        // Prepare Uniform Buffer
        ShaderUniforms uniforms = {
            .time = ctx->time.total,
            .pad0 = 0.0f,
            // Mouse: Normalize relative to the WINDOW (Output), not the Game
            .mousePos = { 
                ctx->input.mousePos.x / (float)ctx->renderer.outputW,
                ctx->input.mousePos.y / (float)ctx->renderer.outputH 
            },

            .resolution = { 
                (float)ctx->renderer.internalW, 
                (float)ctx->renderer.internalH 
            },
            .pad1 = 0.0f
        };


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

    SDL_SubmitGPUCommandBuffer(cmd);
    return true;
}

void Renderer_ReloadShader(EngineContext *ctx) 
{
    SDL_WaitForGPUIdle(ctx->gpu);

    SDL_GPUComputePipeline* newPipeline = _CreateComputePipeline(ctx->gpu);

    if (newPipeline) {
        SDL_ReleaseGPUComputePipeline(ctx->gpu, ctx->renderer.computePipeline);
        ctx->renderer.computePipeline = newPipeline;
        SDL_Log("RENDER: Shader Hot-Reloaded.");
    } else {
        SDL_Log("RENDER: Hot-reload failed.");
    }
}
