#ifndef MATH_SCALAR_H
#define MATH_SCALAR_H

static inline float Float_Clamp(float value, float min_value, float max_value) {
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static inline float Float_ClampAbs(float value, float max_abs) {
    if (value > max_abs) {
        return max_abs;
    }
    if (value < -max_abs) {
        return -max_abs;
    }
    return value;
}

static inline float Float_Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

static inline float Float_SmoothStep(float edge0, float edge1, float value) {
    float t = Float_Clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

#endif // MATH_SCALAR_H
