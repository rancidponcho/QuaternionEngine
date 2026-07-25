#ifndef RENDER_GPU_COMMON_H
#define RENDER_GPU_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <SDL3/SDL_gpu.h>

typedef struct RenderGPUComputePipelineInfo {
    uint32_t sampler_count;
    uint32_t storage_buffer_count;
    uint32_t uniform_buffer_count;
    uint32_t thread_count_x;
    uint32_t thread_count_y;
    uint32_t thread_count_z;
} RenderGPUComputePipelineInfo;

SDL_GPUComputePipeline* RenderGPU_CreateComputePipeline(
    SDL_GPUDevice* gpu,
    const char* shader_name,
    const RenderGPUComputePipelineInfo* info
);

SDL_GPUBuffer* RenderGPU_CreateStorageBuffer(
    SDL_GPUDevice* gpu,
    size_t size_bytes,
    const char* label
);

SDL_GPUTransferBuffer* RenderGPU_CreateUploadTransferBuffer(
    SDL_GPUDevice* gpu,
    size_t size_bytes,
    const char* label
);

bool RenderGPU_UploadBuffer(
    SDL_GPUDevice* gpu,
    SDL_GPUCommandBuffer* cmd,
    SDL_GPUTransferBuffer* transfer_buffer,
    SDL_GPUBuffer* gpu_buffer,
    const void* src_data,
    size_t size_bytes,
    const char* label
);

#endif // RENDER_GPU_COMMON_H
