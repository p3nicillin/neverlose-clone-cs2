// =================================================================
// create_move.cpp
// Hooks CCSGOInput::CreateMove so we get called every game tick
// BEFORE CS2 sends the command to the server.
// This is the foundation for: ragebot, anti-aim, bhop, no recoil.
// =================================================================

#include "create_move.h"
#include "cheat_core.h"
#include "config.h"
#include "offsets.h"
#include "memory.h"
#include "logger.h"
#include "game_classes.h"
#include <windows.h>
#include <cmath>

bool CreateMoveHook::s_installed = false;

// Original CreateMove function pointer (saved so we can call through)
typedef bool(__thiscall* CreateMove_t)(uintptr_t, float, bool);
static CreateMove_t g_OrigCreateMove = nullptr;
static void**       g_CMVTableSlot   = nullptr;

// ---- Bhop via CreateMove — write ForceJump at perfect tick timing ----
// Called from the game thread during input processing, so timing is exact.
static void DoBhop(uintptr_t localPawn) {
    if (!localPawn) return;
    uintptr_t forceJump = Offsets::Get("dwForceJump");
    if (!forceJump) return;
    if (!(GetAsyncKeyState(VK_SPACE) & 0x8000)) { int r=256; Memory::Write(forceJump,&r,sizeof(r)); return; }
    uint32_t flags    = CS2::Read<uint32_t>(localPawn + 0x3F8);
    bool     onGround = (flags & 1) != 0;
    // PRESS on ground (trigger jump), RELEASE in air (defeat hold-detection)
    int val = onGround ? 65537 : 256;
    Memory::Write(forceJump, &val, sizeof(val));
}

// ---- No recoil via CreateMove ----
// Confirmed CS2 structure (from cs2-dumper client_dll.json):
//   pawn + 0x1490 (m_pAimPunchServices) → CAimPunchServices*
//   component + 0x48 (m_vecCsViewPunchAngle) → Vector3 punch
// m_vecCsViewPunchAngle is the FULL visual punch (no * 2 needed).
// Subtract it from view angles to keep crosshair on target.
static void DoNoRecoil(uintptr_t localPawn) {
    uintptr_t shotsFiredOff = Offsets::Get("m_iShotsFired",       0);
    uintptr_t svcOff        = Offsets::Get("m_pAimPunchServices", 0x1490);
    uintptr_t punchOff      = Offsets::Get("m_vecCsViewPunchAngle", 0x48);
    uintptr_t viewAngAddr   = Offsets::Get("dwViewAngles");
    if (!shotsFiredOff || !viewAngAddr) return;

    int shots = CS2::Read<int>(localPawn + shotsFiredOff);
    if (shots <= 0) return;

    // Follow component pointer chain
    uintptr_t punchSvc = CS2::Read<uintptr_t>(localPawn + svcOff);
    if (!punchSvc) return;

    float px = CS2::Read<float>(punchSvc + punchOff);
    float py = CS2::Read<float>(punchSvc + punchOff + 4);
    if (fabsf(px) < 0.001f && fabsf(py) < 0.001f) return;

    static bool diagOnce = false;
    if (!diagOnce) {
        diagOnce = true;
        char buf[128];
        sprintf_s(buf, "NoRecoil: svc=0x%llX punch=(%.3f,%.3f)", (unsigned long long)punchSvc, px, py);
        Logger::Log(buf);
    }

    Vector3 va = CS2::Read<Vector3>(viewAngAddr);
    va.x -= px;   // m_vecCsViewPunchAngle is already the full visual amount
    va.y -= py;
    if (va.x >  89.f) va.x =  89.f;
    if (va.x < -89.f) va.x = -89.f;
    Memory::Write(viewAngAddr, &va, sizeof(va));
}

// ---- Our hooked CreateMove ----
static bool __fastcall HookedCreateMove(uintptr_t thisptr, float frameTime, bool active) {
    // Call original first so the cmd is built
    bool result = g_OrigCreateMove(thisptr, frameTime, active);

    // Log call count once to confirm hook fires
    static int callCount = 0;
    if (++callCount == 100)
        Logger::Log("CreateMove: firing correctly (100 calls confirmed)");

    if (!active || !g_Cheat) return result;
    Config* cfg = g_Cheat->GetConfig();
    if (!cfg) return result;

    // Get the current built command
    // In CS2: CCSGOInput has GetUserCmd at vtable[8] (approximate)
    using GetCmd_t = CS2UserCmd*(__thiscall*)(uintptr_t);
    void**    vt  = *(void***)thisptr;
    auto GetCmd   = (GetCmd_t)vt[8];
    CS2UserCmd* cmd = GetCmd(thisptr);
    if (!cmd) return result;

    uintptr_t localPawnAddr = Offsets::Get("dwLocalPlayerPawn");
    uintptr_t localPawn     = localPawnAddr ? CS2::Read<uintptr_t>(localPawnAddr) : 0;

    if (cfg->m_bunnyhop && localPawn)
        DoBhop(localPawn);

    if (cfg->m_noRecoil && localPawn)
        DoNoRecoil(localPawn);

    // TODO: ragebot, anti-aim (call their Update here)

    return result;
}

bool CreateMoveHook::Install() {
    uintptr_t inputAddr = Offsets::Get("dwCSGOInput");
    if (!inputAddr) {
        Logger::LogError("CreateMove: dwCSGOInput not found");
        return false;
    }

    uintptr_t input = CS2::Read<uintptr_t>(inputAddr);
    if (!input) {
        Logger::LogError("CreateMove: CCSGOInput pointer is null");
        return false;
    }

    void** vtable = *(void***)input;

    // CreateMove is typically at vtable slot 6 in CS2
    // Scan nearby slots for a plausible function in client.dll range
    uintptr_t clientBase = Memory::GetClientBase();
    int       cmSlot     = -1;

    for (int s = 4; s <= 12; s++) {
        uintptr_t fn = (uintptr_t)vtable[s];
        // Valid candidate: in client.dll and callable
        if (fn > clientBase && fn < clientBase + 0x40000000) {
            // Heuristic: CreateMove functions are >100 bytes
            // For now just try slot 6 as the most common
            if (s == 6) { cmSlot = s; break; }
        }
    }

    if (cmSlot < 0) cmSlot = 6; // default to slot 6

    g_CMVTableSlot  = &vtable[cmSlot];
    g_OrigCreateMove = (CreateMove_t)*g_CMVTableSlot;

    DWORD oldProt;
    VirtualProtect(g_CMVTableSlot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProt);
    *g_CMVTableSlot = (void*)HookedCreateMove;
    VirtualProtect(g_CMVTableSlot, sizeof(void*), oldProt, &oldProt);

    s_installed = true;
    char buf[128];
    sprintf_s(buf, "CreateMove hooked at slot %d (input=0x%llX)", cmSlot, (unsigned long long)input);
    Logger::Log(buf);
    return true;
}

void CreateMoveHook::Uninstall() {
    if (!s_installed || !g_CMVTableSlot || !g_OrigCreateMove) return;
    DWORD old;
    VirtualProtect(g_CMVTableSlot, sizeof(void*), PAGE_EXECUTE_READWRITE, &old);
    *g_CMVTableSlot = (void*)g_OrigCreateMove;
    VirtualProtect(g_CMVTableSlot, sizeof(void*), old, &old);
    s_installed = false;
    g_OrigCreateMove = nullptr;
    g_CMVTableSlot   = nullptr;
    Logger::Log("CreateMove unhooked");
}

// Called from the Present hook render thread for features that
// don't need per-tick precision (ESP, menu) — not from CreateMove.
void CreateMoveHook::OnCreateMove(uintptr_t, CS2UserCmd*, bool) {}
