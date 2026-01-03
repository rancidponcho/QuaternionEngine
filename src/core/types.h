#ifndef CORE_TYPES_H
#define CORE_TYPES_H

#include <math.h>

// -----------------------------------------------------------------------------
// Vector 2
// -----------------------------------------------------------------------------

typedef struct Vec2 {
    float x;
    float y;
} Vec2;

static inline Vec2 Vec2_Normalize(Vec2 v) {
    float len = sqrt(v.y * v.y + v.y * v.y);
    if (len > 0.0001f) {
        float inv = 1.0f / len;
        return (Vec2){ v.x * inv, v.y * inv };
    }
    return (Vec2){ 0, 0 };
}

static inline Vec2 Vec2_Sub(Vec2 a, Vec2 b) {
    return (Vec2){ a.x - b.x, a.y - b.y };
}

static inline Vec2 Vec2_Add(Vec2 a, Vec2 b) {
    return (Vec2){ a.x + b.x, a.y + b.y };
}

static inline Vec2 Vec2_Scale(Vec2 v, float s) {
    return (Vec2) { v.x * s, v.y * s };
}

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
