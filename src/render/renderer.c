#include "renderer.h"
#include "SDL3/SDL_gpu.h"
#include "SDL3/SDL_video.h"
#include <stdio.h> // SDL_Log
#include <wchar.h>

#define BASE_SHORT_SIDE 9 

/* ==========================================================================
 * INTERNAL HELPER FUNCTIONS (Static)
 * ========================================================================== */

/**
 * @brief Loads a raw binary file from disk into heap memory.
 * Used primarily for loading compiled shader bytecode (.spv / .msl).
 */
static void* LoadFile(const char* path, size_t* outSize) {
    SDL_IOStream* io = SDL_IOFromFile(path, "rb");
    if (!io) {
        SDL_Log("ERROR: Failed to open file: %s", path);
        return NULL;
    }
    size_t size = SDL_GetIOSize(io);
    void* data = SDL_malloc(size);
    size_t bytesRead = SDL_ReadIO(io, data, size);
    SDL_CloseIO(io);
    if (bytesRead != size) {
        SDL_Log("ERROR: Short read on file: %s", path);
        SDL_free(data);
        return NULL;
    }
    if (outSize) *outSize = size;
    return data;
}

/**
 * @brief Resolves platform-specific shader extension.
 * Metal (macOS) requires .msl, Vulkan (Linux/Windows) requires .spv.
 */
static const char* GetShaderExtension() {
    #if defined(SDL_PLATFORM_MACOS) || defined(SDL_PLATFORM_IOS)
        return "metallib";
    #else
        return "spv";
    #endif
}

// Internal helper for Dispatch calculation
static void UpdateDispatchGroups(EngineContext* ctx) {
    // Ceiling division to ensure coverage
    ctx->dispatchGroupsX = (ctx->texWidth + 8 - 1) / 8;
    ctx->dispatchGroupsY = (ctx->texHeight + 8 - 1) / 8;
}

/* ==========================================================================
 * PUBLIC API
 * ========================================================================== */

bool Renderer_Init(EngineContext* ctx) {
    // --------------------------------------------------------------------------
    // PIPELINE COMPILATION (Shader Loading)
    // --------------------------------------------------------------------------
    char shaderPath[256];
    const char* ext = GetShaderExtension();
    
    // SDL_GetBasePath returns:
    // - macOS/iOS: The absolute path to the 'Resources' folder inside the Bundle.
    // - PC/Linux: The absolute path to the executable's directory.
    // - Android: NULL (usually), because assets are packed inside the APK.
    const char* basePath = SDL_GetBasePath();

    if (basePath) {
        // Platform is PC, Mac, or iOS (Bundle)
        // We constructed CMake to put shaders in a 'shaders' folder relative to the binary/resource path
        snprintf(shaderPath, sizeof(shaderPath), "%sshaders/BasicCompute.%s", basePath, ext);
        SDL_free(basePath); // IMPORTANT: You must free the string returned by SDL
    } else {
        // Platform is likely Android
        // SDL3 on Android maps relative paths directly to the Asset Manager.
        // Since CMake copies assets to 'assets/shaders', the relative path is just 'shaders/...'
        snprintf(shaderPath, sizeof(shaderPath), "shaders/BasicCompute.%s", ext);
    }

    SDL_Log("Loading shader from: %s", shaderPath);
    size_t codeSize;
    void* code = LoadFile(shaderPath, &codeSize);
    if (!code) {
        return false; // Error logged in LoadFile
    }

    // Determine Entry Point Name
    // Metal (spirv-cross) renames 'main' to 'main0' by default.
    // Vulkan/SPIR-V keeps it as 'main'.
    #if defined(SDL_PLATFORM_MACOS) || defined(SDL_PLATFORM_IOS)
        const char* entryPointName = "main0"; 
    #else
        const char* entryPointName = "main";
    #endif

    // Define the Compute Pipeline State Object (PSO).
    // This bakes the register layout and shader code into a hardware object.
    SDL_GPUComputePipelineCreateInfo pipelineInfo = {
        .code = code,
        .code_size = codeSize,
        .entrypoint = entryPointName,
        .format = GetShaderFormat(),
        // RESOURCE LAYOUT:
        // num_readwrite = 1 maps to Space 1 (Set 1) in Vulkan/SDL.
        .num_readonly_storage_textures = 0,
        .num_readwrite_storage_textures = 1,
        .num_uniform_buffers = 0,
        .threadcount_x = 8,
        .threadcount_y = 8,
        .threadcount_z = 1
    };

    SDL_Log("INFO: Creating Pipeline. RW Textures: %d, Code Size: %zu",
            pipelineInfo.num_readwrite_storage_textures,
            pipelineInfo.code_size);

    ctx->computePipeline = SDL_CreateGPUComputePipeline(ctx->gpu, &pipelineInfo);
    SDL_free(code); // Raw bytecode no longer needed after pipeline creation

    if (!ctx->computePipeline) {
        SDL_Log("CRITICAL: Failed to create Compute Pipeline: %s", SDL_GetError());
        return false;
    }

    // --------------------------------------------------------------------------
    // TEXTURE ALLOCATION
    // --------------------------------------------------------------------------
    // Just call Resize. It calculates the Aspect Ratio, sets texWidth/Height,
    // and allocates the texture for you.
    int w, h;
    SDL_GetWindowSizeInPixels(ctx->window, &w, &h);
    ctx->drawTexture = NULL;
    Renderer_Resize(ctx, w, h);

    return true;
}

void Renderer_Shutdown(EngineContext* ctx) {
    // Wait for the GPU to finish any pending work before destroying resources
    SDL_WaitForGPUIdle(ctx->gpu);

    if (ctx->drawTexture) {
        SDL_ReleaseGPUTexture(ctx->gpu, ctx->drawTexture);
        ctx->drawTexture = NULL;
    }

    if (ctx->computePipeline) {
        SDL_ReleaseGPUComputePipeline(ctx->gpu, ctx->computePipeline);
        ctx->computePipeline = NULL;
    }
}

/**
 * @brief helper function to center viewport in window 
 *
 */
static void UpdateViewportInternal(EngineContext* ctx, int winW, int winH, int scale) {
    // The texture size * scale might be slightly smaller than window size 
    // due to integer division remainders. We center it.
    int finalW = ctx->texWidth * scale;
    int finalH = ctx->texHeight * scale;
    
    ctx->viewport.x = (winW - finalW) / 2;
    ctx->viewport.y = (winH - finalH) / 2;
    ctx->viewport.w = finalW;
    ctx->viewport.h = finalH;
}

void Renderer_Resize(EngineContext* ctx, int winW, int winH) {
    if (winW <= 0 || winH <= 0) return;

    // --- LOGIC: LOCK SHORTEST SIDE TO 720 ---
    // 1. Find the smaller window dimension
    int minDim = (winW < winH) ? winW : winH;

    // 2. Calculate Integer Scale
    // How many times does 720 fit into the smallest side?
    int scale = minDim / BASE_SHORT_SIDE;
    if (scale < 1) scale = 1;

    // 3. Calculate New Resolution
    // This naturally handles both Portrait and Landscape.
    // Wide Window:  winH is small -> scale based on H -> Height becomes ~720.
    // Tall Window:  winW is small -> scale based on W -> Width becomes ~720.
    int newTexW = winW / scale;
    int newTexH = winH / scale;

    // Optimization: If the Integer Grid didn't change, just update centering
    if (ctx->drawTexture && ctx->texWidth == newTexW && ctx->texHeight == newTexH) {
        UpdateViewportInternal(ctx, winW, winH, scale);
        return;
    }

    // --- REALLOCATE ---
    SDL_WaitForGPUIdle(ctx->gpu);
    if (ctx->drawTexture) SDL_ReleaseGPUTexture(ctx->gpu, ctx->drawTexture);

    ctx->texWidth = newTexW;
    ctx->texHeight = newTexH;

    SDL_GPUTextureCreateInfo texInfo = {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT,
        .width = ctx->texWidth,
        .height = ctx->texHeight,
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .usage = SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_TEXTUREUSAGE_SAMPLER
    };
    ctx->drawTexture = SDL_CreateGPUTexture(ctx->gpu, &texInfo);
    
    UpdateDispatchGroups(ctx);
    UpdateViewportInternal(ctx, winW, winH, scale);
}

/*
 *
 *
 *
 */
void Renderer_Draw(EngineContext* ctx) {
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(ctx->gpu);
    SDL_GPUTexture* swapchainTex;
    Uint32 w, h;

    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, ctx->window, &swapchainTex, &w, &h)) {
        SDL_CancelGPUCommandBuffer(cmd);
        return;
    }

    if (swapchainTex != NULL) {
        // --- COMPUTE ---
        SDL_GPUStorageTextureReadWriteBinding storageBinding = {
            .texture = ctx->drawTexture, .mip_level = 0, .layer = 0, .cycle = false
        };

        SDL_GPUComputePass* computePass = SDL_BeginGPUComputePass(cmd, &storageBinding, 1, NULL, 0);
        SDL_BindGPUComputePipeline(computePass, ctx->computePipeline);
        SDL_DispatchGPUCompute(computePass, ctx->dispatchGroupsX, ctx->dispatchGroupsY, 1);
        SDL_EndGPUComputePass(computePass);

        // --- BLIT ---
        SDL_GPUBlitInfo blitInfo = {
            .source.texture = ctx->drawTexture,
            .source.w = ctx->texWidth,
            .source.h = ctx->texHeight,

            .destination.texture = swapchainTex,
            .destination.x = ctx->viewport.x,
            .destination.y = ctx->viewport.y,
            .destination.w = ctx->viewport.w, 
            .destination.h = ctx->viewport.h,

            .load_op = SDL_GPU_LOADOP_CLEAR,
            .clear_color = {0, 0, 0, 1}, // Black bars
            .filter = SDL_GPU_FILTER_NEAREST
        };

        SDL_BlitGPUTexture(cmd, &blitInfo);
        SDL_SubmitGPUCommandBuffer(cmd);
    }
}
