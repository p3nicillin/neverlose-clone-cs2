// =================================================================
// aimbot.cpp - CS2 in-process aimbot
// =================================================================

#include "aimbot.h"
#include "create_move.h"
#include "cheat_core.h"
#include "config.h"
#include "ui_manager.h"
#include "offsets.h"
#include "memory.h"
#include "game_classes.h"
#include "no_spread.h"
#include "logger.h"
#include <cmath>
#include <windows.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static constexpr float RAD2DEG = 180.f / (float)M_PI;
static constexpr float DEG2RAD = (float)M_PI / 180.f;

Vector3 Aimbot::NormAngles(Vector3 a) {
    while (a.x >  89.f)  a.x -= 180.f;
    while (a.x < -89.f)  a.x += 180.f;
    while (a.y >  180.f) a.y -= 360.f;
    while (a.y < -180.f) a.y += 360.f;
    a.z = 0.f;
    return a;
}

Vector3 Aimbot::CalcAngle(const Vector3& src, const Vector3& dst) {
    float dx = dst.x - src.x;
    float dy = dst.y - src.y;
    float dz = dst.z - src.z;
    float dist2d = sqrtf(dx*dx + dy*dy);
    return NormAngles({ -atan2f(dz, dist2d) * RAD2DEG,
                         atan2f(dy, dx)      * RAD2DEG, 0.f });
}

float Aimbot::CalcFov(const Vector3& va, const Vector3& aa) {
    float dp = va.x - aa.x;
    float dy = va.y - aa.y;
    while (dy >  180.f) dy -= 360.f;
    while (dy < -180.f) dy += 360.f;
    return sqrtf(dp*dp + dy*dy);
}

// Logs a reason string only when it changes, so hot-path diagnostics never spam.
static void AimTrace(const char* reason) {
    static const char* last = nullptr;
    if (reason != last) { last = reason; Logger::Log("[AIMBOT] %s", reason); }
}

void Aimbot::Update(void* pInput) {
    Config* cfg = g_Cheat ? g_Cheat->GetConfig() : nullptr;
    if (!cfg || !cfg->m_aimbotEnabled) { AimTrace("disabled (cfg off)"); return; }

    int aimKey = cfg->m_aimbotKey ? cfg->m_aimbotKey : VK_LBUTTON;
    if (!(GetAsyncKeyState(aimKey) & 0x8000)) { AimTrace("aim key not held"); return; }
    if (g_Cheat && g_Cheat->GetUI() && g_Cheat->GetUI()->IsMenuOpen()) { AimTrace("menu open"); return; }

    uintptr_t localCtrlAddr = Offsets::Get("dwLocalPlayerController");
    uintptr_t listAddr      = Offsets::Get("dwEntityList");
    uintptr_t viewAngAddr   = Offsets::Get("dwViewAngles");
    if (!localCtrlAddr || !listAddr || !viewAngAddr) { AimTrace("null offset (ctrl/list/viewang)"); return; }

    uintptr_t localCtrl  = CS2::Read<uintptr_t>(localCtrlAddr);
    uintptr_t entityList = CS2::Read<uintptr_t>(listAddr);
    if (!localCtrl || !entityList) { AimTrace("null localCtrl/entityList read (stale ptr offsets)"); return; }

    uintptr_t localPawn = CS2::GetPawn(entityList, localCtrl);
    if (!localPawn) { AimTrace("null localPawn"); return; }
    AimTrace("active — scanning for target");

    Vector3 origin  = CS2::GetAbsOrigin(localPawn);
    Vector3 viewOffset = CS2::Read<Vector3>(localPawn + Offsets::Get("m_vecViewOffset", 0xE78));
    Vector3 eyePos  = origin + viewOffset;
    // Always read the current view from the authoritative dwViewAngles global.
    // The CCSGOInput input-angle offset (0xBE0) is an unverified guess and the
    // runtime auto-discovery for it is failing, so reading it yields garbage,
    // which made newAngle (= view + delta) snap to a wrong absolute direction.
    Vector3 viewAng = CS2::Read<Vector3>(viewAngAddr);

    int     myTeam  = CS2::GetTeam(localPawn);

    uintptr_t bestPawn = 0;
    Vector3   bestHead = {};
    float     bestFov  = cfg->m_aimbotFov;

    for (int i = 1; i <= 64; ++i) {
        uintptr_t ctrl = CS2::GetEntityByIndex(entityList, i);
        if (!ctrl || ctrl == localCtrl) continue;
        uintptr_t pawn = CS2::GetPawn(entityList, ctrl);
        if (!pawn) continue;
        int team = CS2::GetTeam(pawn);
        if (team != 2 && team != 3) continue;
        if (!cfg->m_aimbotTeamcheck && team == myTeam) continue;
        int hp = CS2::GetHealth(pawn);
        if (hp <= 0 || hp > 100) continue;
        if (cfg->m_aimbotVisibleCheck && !NoSpread::IsVisible(localPawn, pawn)) continue;
        Vector3 pos = CS2::GetAbsOrigin(pawn);
        if (pos.x == 0.f && pos.y == 0.f) continue;
        Vector3 head = { pos.x, pos.y, pos.z + 62.f };
        uintptr_t boneArr = CS2::GetBoneArray(pawn);
        if (boneArr) {
            Vector3 hb = CS2::GetBonePos(boneArr, 7);
            // Only trust the bone position if it is physically near the pawn
            // origin. A stale/wrong bone-array offset yields finite-but-garbage
            // coordinates far from the player, which would otherwise push the
            // target outside the FOV and silently disable the aimbot.
            float dx = hb.x - pos.x, dy = hb.y - pos.y, dz = hb.z - pos.z;
            if (std::isfinite(hb.x) && std::isfinite(hb.y) && std::isfinite(hb.z) &&
                (hb.x != 0.f || hb.y != 0.f || hb.z != 0.f) &&
                fabsf(dx) < 40.f && fabsf(dy) < 40.f && dz > 20.f && dz < 100.f) {
                head = hb;
            }
        }
        Vector3 ang  = CalcAngle(eyePos, head);
        float   fov  = CalcFov(viewAng, ang);
        if (fov < bestFov) { bestFov = fov; bestPawn = pawn; bestHead = head; }
    }

    if (!bestPawn) { AimTrace("no target within FOV"); return; }
    AimTrace("TARGET ACQUIRED — applying angle");

    Vector3 targetAng = CalcAngle(eyePos, bestHead);
    if (cfg->m_legitbotRcs > 0.f) {
        uintptr_t punchSvc = CS2::Read<uintptr_t>(localPawn + Offsets::Get("m_pAimPunchServices", 0x14B8));
        if (punchSvc) {
            float scale = cfg->m_legitbotRcs * 0.01f;
            uintptr_t po = Offsets::Get("m_vecCsViewPunchAngle", 0x48);
            targetAng.x -= CS2::Read<float>(punchSvc + po) * scale;
            targetAng.y -= CS2::Read<float>(punchSvc + po + 4) * scale;
        }
    }
    float smooth = (cfg->m_aimbotSmooth < 1.f) ? 1.f : cfg->m_aimbotSmooth;

    // Write only the DELTA so mouse movement is still honoured
    float dPitch = targetAng.x - viewAng.x;
    float dYaw   = targetAng.y - viewAng.y;
    while (dYaw >  180.f) dYaw -= 360.f;
    while (dYaw < -180.f) dYaw += 360.f;

    Vector3 newAng;
    newAng.x = viewAng.x + dPitch / smooth;
    newAng.y = viewAng.y + dYaw   / smooth;
    newAng.z = 0.f;
    newAng   = NormAngles(newAng);

    // Write straight to the dwViewAngles global. ApplyAngle also writes the
    // CCSGOInput command viewangles via offsets whose auto-discovery is failing,
    // corrupting the view; the global write is authoritative and moves the aim
    // reliably for a legit assist.
    Memory::Write(viewAngAddr, &newAng, sizeof(newAng));
    (void)pInput;
}
