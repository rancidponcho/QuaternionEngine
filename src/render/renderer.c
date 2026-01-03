/*
================================================================================
    Renderer
    Handles the high-level graphics pipeline:
    - Shader loading & compilation
    - Resolution scaling (720p logic)
    - Compute Dispatch & Blitting
================================================================================
*/

#include "renderer.h"

#include "SDL3/SDL_gpu.h"
#include "core/types.h"
#include <SDL3/SDL_log.h>
#include <stdio.h>

#define BASE_SHORT_SIDE 100

// -----------------------------------------------------------------------------
// Internal Helpers
// -----------------------------------------------------------------------------

/*
    LoadFile
    Reads raw binary assets (SPIR-V / Metallib) from disk.
*/
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

/*
    GetShaderExtension
    Returns "metallib" (Apple) or "spv" (Vulkan/Generic) for file paths.
*/
static const char* GetShaderExtension(void) {
    #if defined(SDL_PLATFORM_MACOS) || defined(SDL_PLATFORM_IOS)
        return "metallib";
    #else
        return "spv";
    #endif
}

// Internal helper for Dispatch calculation
static void UpdateDispatchGroups(EngineContext* ctx) {
    // Ceiling division to ensure coverage
    ctx->dispatchX = (ctx->internalW + 8 - 1) / 8;
    ctx->dispatchY = (ctx->internalH + 8 - 1) / 8;
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

bool Renderer_Init(EngineContext* ctx) {
    // -------------------------------------------------------------------------
    // Shader Path Resolution
    // -------------------------------------------------------------------------
    char shaderPath[256];
    const char* ext = GetShaderExtension();
    const char* basePath = SDL_GetBasePath();

    if (basePath) {
        // PC/Mac/iOS: Assets are in a specific Resources/bin folder
        snprintf(shaderPath, sizeof(shaderPath), "%sshaders/BasicCompute.%s", basePath, ext);
    } else {
        // Android: Assets are relative to the APK root
        snprintf(shaderPath, sizeof(shaderPath), "shaders/BasicCompute.%s", ext);
    }

    SDL_Log("RENDER: Loading Shader: %s", shaderPath);

    // -------------------------------------------------------------------------
    // Pipeline Creation
    // -------------------------------------------------------------------------
    size_t codeSize;
    void* code = LoadFile(shaderPath, &codeSize);
    if (!code) return false;

    // Metal (via SPIRV-Cross) usually renames main -> main0
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

    ctx->computePipeline = SDL_CreateGPUComputePipeline(ctx->gpu, &pipelineInfo);
    SDL_free(code);

    if (!ctx->computePipeline) {
        SDL_LogCritical(SDL_LOG_CATEGORY_RENDER, "Pipeline Creation Failed: %s", SDL_GetError());
        return false;
    }

    // -------------------------------------------------------------------------
    // Initial Resize
    // -------------------------------------------------------------------------
    int w, h;
    SDL_GetWindowSizeInPixels(ctx->window, &w, &h);
    Renderer_Resize(ctx, w, h);

    return true;
}

void Renderer_Shutdown(EngineContext* ctx) {
    SDL_WaitForGPUIdle(ctx->gpu);

    if (ctx->computePipeline) {
        SDL_ReleaseGPUComputePipeline(ctx->gpu, ctx->computePipeline);
        ctx->computePipeline = NULL;
    }
}

void Renderer_Resize(EngineContext* ctx, int winW, int winH) {
    if (winW <= 0 || winH <= 0) return;

    ctx->outputW = winW;
    ctx->outputH = winH;

    // -------------------------------------------------------------------------
    // Aspect Ratio Logic (Pixel Art Scaling)
    // -------------------------------------------------------------------------
    // Lock the shortest side to 720p, calculate integer scale.
    int minDim = (winW < winH) ? winW : winH;
    int scale = minDim / BASE_SHORT_SIDE;
    if (scale < 1) scale = 1;

    int newTexW = winW / scale;
    int newTexH = winH / scale;

    // -------------------------------------------------------------------------
    // Reallocation
    // -------------------------------------------------------------------------
    // Delegate the dirty work to the Engine Core
    // Only resize if the integer grid actually changed
    if (ctx->internalW != newTexW || ctx->internalH != newTexH) {
        Engine_ResizeTexture(ctx, newTexW, newTexH);
        
        // Update simulation dispatch groups to match new texture size
        ctx->dispatchX = (newTexW + 7) / 8;
        ctx->dispatchY = (newTexH + 7) / 8;
    }

    // -------------------------------------------------------------------------
    // Viewport Centering
    // -------------------------------------------------------------------------
    int finalW = ctx->internalW * scale;
    int finalH = ctx->internalH * scale;
    
    ctx->viewport.x = (winW - finalW) / 2;
    ctx->viewport.y = (winH - finalH) / 2;
    ctx->viewport.w = finalW;
    ctx->viewport.h = finalH;
}

bool Renderer_Draw(EngineContext* ctx) {
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
        // ---------------------------------------------------------------------
        // Prepare Uniform Buffer
        // ---------------------------------------------------------------------
        ShaderUniforms uniforms = {
            .time = ctx->time.total,
            .pad0 = 0.0f,
            // Mouse: Normalize relative to the WINDOW (Output), not the Game
            .mousePos = { 
                ctx->input.mousePos.x / (float)ctx->outputW,
                ctx->input.mousePos.y / (float)ctx->outputH 
            },

            .resolution = { 
                (float)ctx->internalW, 
                (float)ctx->internalH 
            },
            .pad1 = 0.0f
        };


        // ---------------------------------------------------------------------
        // Compute Pass
        // ---------------------------------------------------------------------
        SDL_GPUStorageTextureReadWriteBinding storageBinding = {
            .texture = ctx->drawTexture, 
            .mip_level = 0, 
            .layer = 0, 
            .cycle = true
        };

        SDL_GPUComputePass* computePass = SDL_BeginGPUComputePass(
            cmd,
            &storageBinding,
            1,
            NULL,
            0
        );

        SDL_BindGPUComputePipeline(computePass, ctx->computePipeline);
        SDL_PushGPUComputeUniformData(cmd, 0, &uniforms, sizeof(uniforms));
        SDL_DispatchGPUCompute(computePass, ctx->dispatchX, ctx->dispatchY, 1);
        SDL_EndGPUComputePass(computePass);

        // ---------------------------------------------------------------------
        // Blit Pass (Scale Internal -> Window)
        // ---------------------------------------------------------------------
        SDL_GPUBlitInfo blitInfo = {
            .source.texture = ctx->drawTexture,
            .source.w = ctx->internalW,
            .source.h = ctx->internalH,

            .destination.texture = swapchainTex,
            .destination.x = ctx->viewport.x,
            .destination.y = ctx->viewport.y,
            .destination.w = ctx->viewport.w, 
            .destination.h = ctx->viewport.h,

            .load_op = SDL_GPU_LOADOP_CLEAR,
            .clear_color = {0, 0, 0, 1}, // Letterbox color
            .filter = SDL_GPU_FILTER_NEAREST
        };

        SDL_BlitGPUTexture(cmd, &blitInfo);
    }

    SDL_SubmitGPUCommandBuffer(cmd);
    return true;
}
