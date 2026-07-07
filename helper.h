#pragma once

#include <math.h>

struct Vector2
{
    float x;
    float y;
};

static inline float Magnitude(const Vector2 &v)
{
    return sqrtf(v.x * v.x + v.y * v.y);
}

static inline Vector2 normalize(const Vector2 &v)
{
    float m = Magnitude(v);
    if (m == 0.0f)
        return {0.0f, 0.0f};
    return {v.x / m, v.y / m};
}

static inline float SquaredDistance(const Vector2 &a, const Vector2 &b)
{
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    return dx * dx + dy * dy;
}
