#ifndef VEC_H
#define VEC_H

#include <math.h>

// -----------------------------------------------------------------------------
// Vector 2
// -----------------------------------------------------------------------------

typedef struct Vec2 {
    float x;
    float y;
} Vec2;

static inline float Vec2_Dot(Vec2 a, Vec2 b) {
    return a.x * b.x + a.y * b.y;
}

static inline float Vec2_LengthSq(Vec2 v) {
    return Vec2_Dot(v, v);
}

static inline float Vec2_Length(Vec2 v) {
    return sqrtf(Vec2_LengthSq(v));
}

static inline Vec2 Vec2_Normalize(Vec2 v) {
    float len = Vec2_Length(v);
    if (len > 0.0001f) {
        float inv = 1.0f / len;
        return (Vec2){ v.x * inv, v.y * inv };
    }
    return (Vec2){ 0, 0 };
}

static inline Vec2 Vec2_NormalizeOr(Vec2 v, Vec2 fallback) {
    float len = Vec2_Length(v);
    if (len > 0.0001f) {
        float inv = 1.0f / len;
        return (Vec2){ v.x * inv, v.y * inv };
    }
    return fallback;
}

static inline Vec2 Vec2_Sub(Vec2 a, Vec2 b) {
    return (Vec2){ a.x - b.x, a.y - b.y };
}

static inline float Vec2_DistanceSq(Vec2 a, Vec2 b) {
    return Vec2_LengthSq(Vec2_Sub(a, b));
}

static inline Vec2 Vec2_Add(Vec2 a, Vec2 b) {
    return (Vec2){ a.x + b.x, a.y + b.y };
}

static inline Vec2 Vec2_Scale(Vec2 v, float s) {
    return (Vec2) { v.x * s, v.y * s };
}

static inline Vec2 Vec2_Lerp(Vec2 a, Vec2 b, float t) {
    return (Vec2){
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t
    };
}

static inline Vec2 Vec2_Limit(Vec2 v, float max_length) {
    if (max_length <= 0.0f) {
        return (Vec2){ 0, 0 };
    }

    float length_sq = Vec2_LengthSq(v);
    if (length_sq <= max_length * max_length) {
        return v;
    }

    return Vec2_Scale(v, max_length / sqrtf(length_sq));
}

#endif // VEC_H
