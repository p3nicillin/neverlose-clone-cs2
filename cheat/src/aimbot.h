#pragma once
#include "game_classes.h"

// Simple CS2 in-process aimbot.
// Reads/writes CCSGOInput view angles every game tick.
class Aimbot {
public:
    static void Update();

    static float CalcFov(const Vector3& va, const Vector3& aa);
    static Vector3 CalcAngle(const Vector3& src, const Vector3& dst);
    static Vector3 NormAngles(Vector3 a);
};
