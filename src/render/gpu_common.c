#include "render/gpu_common.h"

#include <stdio.h>
#include <string.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

static SDL_GPUShaderFormat RenderGPU_ShaderFormat(void) {
    #if defined(SDL_PLATFORM_MACOS) || defined(SDL_PLATFORM_IOS)
        return SDL_GPU_SHADERFORMAT_METALLIB;
    #else
        return SDL_GPU_SHADERFORMAT_SPIRV;
    #endif
}

static const char* RenderGPU_ShaderExtension(void) {
    #if defined(SDL_PLATFORM_MACOS) || defined(SDL_PLATFORM_IOS)
        return "metallib";
    #else
        return "spv";
    #endif
}

static const char* RenderGPU_ShaderEntryPoint(void) {
    return RenderGPU_ShaderFormat() == SDL_GPU_SHADERFORMAT_METALLIB ? "main0" : "main";
}

static void* RenderGPU_LoadFile(const char* path, size_t* out_size) {
    SDL_IOStream* io = SDL_IOFromFile(path, "rb");
    if (!io) {
        SDL_Log("RENDER: Failed to open shader: %s", path);
        return NULL;
    }

    Sint64 file_size = SDL_GetIOSize(io);
    if (file_size <= 0) {
        SDL_Log("RENDER: Empty shader file: %s", path);
        SDL_CloseIO(io);
        return NULL;
    }

    size_t size = (size_t)file_size;
    void* data = SDL_malloc(size);
    if (!data) {
        SDL_CloseIO(io);
        return NULL;
    }

    if (SDL_ReadIO(io, data, size) != size) {
        SDL_Log("RENDER: Short read on shader: %s", path);
        SDL_free(data);
        SDL_CloseIO(io);
        return NULL;
    }

    SDL_CloseIO(io);
    if (out_size) {
        *out_size = size;
    }
    return data;
}

SDL_GPUComputePipeline* RenderGPU_CreateComputePipeline(
    SDL_GPUDevice* gpu,
    const char* shader_name,
    const RenderGPUComputePipelineInfo* info
) {
    char shader_path[256];
    const char* base_path = SDL_GetBasePath();
    const char* ext = RenderGPU_ShaderExtension();

    if (base_path) {
        snprintf(shader_path, sizeof(shader_path), "%sassets/shaders/%s.%s", base_path, shader_name, ext);
    } else {
        snprintf(shader_path, sizeof(shader_path), "shaders/%s.%s", shader_name, ext);
    }

    size_t code_size = 0;
    void* code = RenderGPU_LoadFile(shader_path, &code_size);
    if (!code) {
        return NULL;
    }

    SDL_GPUComputePipelineCreateInfo pipeline_info = {
        .code = code,
        .code_size = code_size,
        .entrypoint = RenderGPU_ShaderEntryPoint(),
        .format = RenderGPU_ShaderFormat(),
        .num_samplers = info->sampler_count,
        .num_readonly_storage_textures = 0,
        .num_readonly_storage_buffers = info->storage_buffer_count,
        .num_readwrite_storage_textures = 1,
        .num_readwrite_storage_buffers = 0,
        .num_uniform_buffers = info->uniform_buffer_count,
        .threadcount_x = info->thread_count_x,
        .threadcount_y = info->thread_count_y,
        .threadcount_z = info->thread_count_z,
        .props = 0
    };

    SDL_GPUComputePipeline* pipeline = SDL_CreateGPUComputePipeline(gpu, &pipeline_info);
    SDL_free(code);
    return pipeline;
}

SDL_GPUBuffer* RenderGPU_CreateStorageBuffer(
    SDL_GPUDevice* gpu,
    size_t size_bytes,
    const char* label
) {
    SDL_GPUBufferCreateInfo info = {
        .usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ,
        .size = size_bytes,
        .props = 0
    };

    SDL_GPUBuffer* buffer = SDL_CreateGPUBuffer(gpu, &info);
    if (!buffer) {
        SDL_LogCritical(SDL_LOG_CATEGORY_RENDER, "Failed to create %s GPU buffer: %s", label, SDL_GetError());
    }

    return buffer;
}

SDL_GPUTransferBuffer* RenderGPU_CreateUploadTransferBuffer(
    SDL_GPUDevice* gpu,
    size_t size_bytes,
    const char* label
) {
    SDL_GPUTransferBufferCreateInfo info = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = size_bytes,
        .props = 0
    };

    SDL_GPUTransferBuffer* buffer = SDL_CreateGPUTransferBuffer(gpu, &info);
    if (!buffer) {
        SDL_LogCritical(SDL_LOG_CATEGORY_RENDER, "Failed to create %s transfer buffer: %s", label, SDL_GetError());
    }

    return buffer;
}

bool RenderGPU_UploadBuffer(
    SDL_GPUDevice* gpu,
    SDL_GPUCommandBuffer* cmd,
    SDL_GPUTransferBuffer* transfer_buffer,
    SDL_GPUBuffer* gpu_buffer,
    const void* src_data,
    size_t size_bytes,
    const char* label
) {
    void* mapped = SDL_MapGPUTransferBuffer(gpu, transfer_buffer, true);
    if (!mapped) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Failed to map %s transfer buffer: %s", label, SDL_GetError());
        return false;
    }

    memcpy(mapped, src_data, size_bytes);
    SDL_UnmapGPUTransferBuffer(gpu, transfer_buffer);

    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd);
    if (!copy_pass) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Failed to begin %s copy pass: %s", label, SDL_GetError());
        return false;
    }

    SDL_GPUTransferBufferLocation src = {
        .transfer_buffer = transfer_buffer,
        .offset = 0
    };

    SDL_GPUBufferRegion dst = {
        .buffer = gpu_buffer,
        .offset = 0,
        .size = size_bytes
    };

    SDL_UploadToGPUBuffer(copy_pass, &src, &dst, true);
    SDL_EndGPUCopyPass(copy_pass);
    return true;
}
