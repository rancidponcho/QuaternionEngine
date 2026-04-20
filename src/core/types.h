#ifndef CORE_TYPES_H
#define CORE_TYPES_H

#include "math/vec.h"

// -----------------------------------------------------------------------------
// Shader Uniforms (GPU Layout)
// -----------------------------------------------------------------------------
// 16-byte alignment
typedef struct ShaderUniforms {
    float   time;       // Time in seconds
    float   pad0;
    Vec2    mousePos;   // Normalized
    Vec2    resolution; // Internal Resolution
    Vec2    pad1;
} ShaderUniforms;

#endif // CORE_TYPES_H
