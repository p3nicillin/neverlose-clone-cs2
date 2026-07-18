// =================================================================
// triggerbot.cpp — auto-fire when crosshair is on enemy
// =================================================================

#include "triggerbot.h"
#include "create_move.h"
#include "game_classes.h"
#include "offsets.h"
#include "memory.h"
#include "cheat_core.h"
#include "config.h"
#include "ui_manager.h"
#include "no_spread.h"
#include "aimbot.h"
#include <windows.h>
#include <cmath>

void Triggerbot::Update(void* pInput) {
    Config* cfg = g_Cheat ? g_Cheat->GetConfig() : nullptr;
    if (!cfg || !cfg->m_triggerbotEnabled) return;

    // Safety: only when CS2 is foreground and menu is closed
    if (GetForegroundWindow() == NULL) return;
    if (g_Cheat && g_Cheat->GetUI() && g_Cheat->GetUI()->IsMenuOpen()) return;

    uintptr_t localPawnAddr = Offsets::Get("dwLocalPlayerPawn");
    uintptr_t localCtrlAddr = Offsets::Get("dwLocalPlayerController");
    uintptr_t listAddr      = Offsets::Get("dwEntityList");
    uintptr_t viewAngAddr   = Offsets::Get("dwViewAngles");
    if (!localPawnAddr || !localCtrlAddr || !listAddr || !viewAngAddr) return;

    uintptr_t localPawn  = CS2::Read<uintptr_t>(localPawnAddr);
    uintptr_t localCtrl  = CS2::Read<uintptr_t>(localCtrlAddr);
    uintptr_t entityList = CS2::Read<uintptr_t>(listAddr);
    if (!localPawn || !localCtrl || !entityList) return;

    // Don't trigger if player is dead
    int hp = CS2::GetHealth(localPawn);
    if (hp <= 0) return;

    Vector3 origin  = CS2::GetAbsOrigin(localPawn);
    if (origin.x == 0.f && origin.y == 0.f) return;

    Vector3 viewOffset = CS2::Read<Vector3>(localPawn + Offsets::Get("m_vecViewOffset", 0xE78));
    Vector3 eyePos  = origin + viewOffset;
    // Read the current view from the authoritative dwViewAngles global. The
    // CCSGOInput input-angle offset (0xBE0) is an unverified guess whose runtime
    // auto-discovery is failing, so reading it gave garbage and broke the FOV test.
    Vector3 viewAng = CS2::Read<Vector3>(viewAngAddr);

    int     myTeam  = CS2::GetTeam(localPawn);

    bool onTarget = false;

    for (int i = 1; i <= 64 && !onTarget; ++i) {
        uintptr_t ctrl = CS2::GetEntityByIndex(entityList, i);
        if (!ctrl || ctrl == localCtrl) continue;
        uintptr_t pawn = CS2::GetPawn(entityList, ctrl);
        if (!pawn) continue;
        int team = CS2::GetTeam(pawn);
        if (team != 2 && team != 3) continue;
        if (team == myTeam) continue;
        int eh = CS2::GetHealth(pawn);
        if (eh <= 0 || eh > 100) continue;
        if (cfg->m_triggerbotVisibleCheck && !NoSpread::IsVisible(localPawn, pawn)) continue;

        Vector3 pos  = CS2::GetAbsOrigin(pawn);
        if (pos.x == 0.f && pos.y == 0.f) continue;

        // Check multiple hitboxes (head, chest, stomach)
        const float headZ = 62.f, chestZ = 40.f, stomachZ = 20.f;
        for (float zOff : { headZ, chestZ, stomachZ }) {
            Vector3 target = { pos.x, pos.y, pos.z + zOff };
            Vector3 ang    = Aimbot::CalcAngle(eyePos, target);
            float   fov    = Aimbot::CalcFov(viewAng, ang);
            if (fov < cfg->m_triggerbotFov) { onTarget = true; break; }
        }
    }

    // Fire via SendInput mouse click. The command-path (SetAttack) writes the
    // CUserCmd button through offsets whose auto-discovery is failing, so it
    // never actually shot; a synthesized click works regardless of offsets.
    static DWORD fireSince  = 0;
    static bool  mouseDown  = false;
    DWORD now = GetTickCount();

    if (onTarget) {
        if (fireSince == 0) fireSince = now;
        if (now - fireSince >= (DWORD)cfg->m_triggerbotDelay && !mouseDown) {
            INPUT inp = {}; inp.type = INPUT_MOUSE;
            inp.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
            SendInput(1, &inp, sizeof(INPUT));
            mouseDown = true;
        }
    } else {
        fireSince = 0;
        if (mouseDown) {
            INPUT inp = {}; inp.type = INPUT_MOUSE;
            inp.mi.dwFlags = MOUSEEVENTF_LEFTUP;
            SendInput(1, &inp, sizeof(INPUT));
            mouseDown = false;
        }
    }
    (void)pInput;
}
