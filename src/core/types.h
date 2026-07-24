#ifndef CORE_TYPES_H
#define CORE_TYPES_H

#include <stdint.h>

#include "math/vec.h"

// -----------------------------------------------------------------------------
// Shader Uniforms (GPU Layout)
// -----------------------------------------------------------------------------
// 16-byte alignment
typedef struct ShaderUniforms {
    Vec2    resolution; // Internal Resolution
    uint32_t matterNodeCount;
    float   matterThreshold;
    Vec2    viewOrigin;
    Vec2    cursorPos;
    uint32_t cursorVisible;
    uint32_t physicsDebugFlags;
    float   physicsDebugCircleWidth;
    float   physicsDebugFieldWidth;
} ShaderUniforms;

#endif // CORE_TYPES_H
