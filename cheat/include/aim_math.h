// =================================================================
// aim_math.h - Shared angle/FOV/distance math for ragebot & legitbot
// =================================================================
#pragma once

#include "game_classes.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

inline constexpr float kRad = (float)(M_PI / 180.0);

inline Vector3 NormAngles(Vector3 a) {
    while (a.x > 89.f)   a.x -= 180.f;
    while (a.x < -89.f)  a.x += 180.f;
    while (a.y > 180.f)  a.y -= 360.f;
    while (a.y < -180.f) a.y += 360.f;
    a.z = 0.f;
    return a;
}

inline Vector3 CalcAngle(const Vector3& src, const Vector3& dst) {
    float dx = dst.x - src.x;
    float dy = dst.y - src.y;
    float dz = dst.z - src.z;
    float dist2d = sqrtf(dx * dx + dy * dy);
    return NormAngles({ -atan2f(dz, dist2d) * (180.f / (float)M_PI),
                          atan2f(dy, dx)     * (180.f / (float)M_PI), 0.f });
}

inline float CalcFov(const Vector3& va, const Vector3& aa) {
    float dp = va.x - aa.x;
    float dy = va.y - aa.y;
    while (dy > 180.f) dy -= 360.f;
    while (dy < -180.f) dy += 360.f;
    return sqrtf(dp * dp + dy * dy);
}

inline float Dist3D(const Vector3& a, const Vector3& b) {
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return sqrtf(dx * dx + dy * dy + dz * dz);
}
