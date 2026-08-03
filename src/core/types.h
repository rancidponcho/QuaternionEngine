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
    Vec2    miningBeamStart;
    Vec2    miningBeamEnd;
    uint32_t miningBeamActive;
    uint32_t miningBeamHit;
    float   miningBeamWidth;
    float   miningTipRadius;
    Vec2    miningToolStart;
    Vec2    miningToolEnd;
    uint32_t miningToolActive;
    uint32_t miningToolFiring;
    float   miningToolWidth;
    float   miningToolTipRadius;
} ShaderUniforms;

#endif // CORE_TYPES_H
