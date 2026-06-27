// =================================================================
// misc.cpp - Misc features
// =================================================================

#include "misc.h"
#include "game_classes.h"
#include "offsets.h"
#include "memory.h"
#include "cheat_core.h"
#include "config.h"
#include "logger.h"
#include <windows.h>
#include <cmath>

Misc* g_Misc = nullptr;

Misc::Misc() : m_knifeBot(false), m_voteReveal(false), m_skinChanger(false),
    m_nameSpammer(false), m_clanTagSpammer(false), m_autoAccept(false),
    m_rankRevealer(false), m_damageReport(false), m_hudRemoval(false),
    m_skyboxRemoval(false), m_shadowRemoval(false), m_scopeRemoval(false),
    m_fogRemoval(false), m_smokeRemoval(false), m_flashReduction(false),
    m_flashAmount(0.3f), m_chatSpamBlock(false), m_messageFilter(false),
    m_autoPistol(false), m_autoReload(false) {}

void Misc::Update() {
    Config* cfg = g_Cheat ? g_Cheat->GetConfig() : nullptr;
    if (!cfg) return;

    uintptr_t localPawnAddr = Offsets::Get("dwLocalPlayerPawn");
    if (!localPawnAddr) return;
    uintptr_t localPawn = CS2::Read<uintptr_t>(localPawnAddr);
    if (!localPawn) return;

    // ----------------------------------------------------------------
    // Bunny hop — skip for now, dwForceJump unused in CS2 2025
    // ----------------------------------------------------------------

    // ----------------------------------------------------------------
    // No recoil
    // Confirmed offsets from cs2-dumper client_dll.json:
    //   pawn + 0x1490 (m_pAimPunchServices) → CAimPunchServices*
    //   component + 0x48 (m_vecCsViewPunchAngle) → Vector3 punch
    //   pawn + 0x1C64 (m_iShotsFired) → int
    // Guard: only apply when shots > 0 (prevents WASD camera spin).
    // ----------------------------------------------------------------
    if (cfg->m_noRecoil) {
        uintptr_t viewAngAddr = Offsets::Get("dwViewAngles");
        if (viewAngAddr) {
            int shots = CS2::Read<int>(localPawn + 0x1C64);  // m_iShotsFired

            uintptr_t punchSvc = CS2::Read<uintptr_t>(localPawn + 0x1490);

            // m_vecCsViewPunchAngle[0x48] = yaw component (horizontal)
            // m_vecCsViewPunchAngle[0x4C] = pitch component (vertical)  ← SWAPPED from typical Vector3
            // m_flAimPitchAngle[0x498] on pawn = aim pitch (alternative source)
            float yawPunch   = punchSvc ? CS2::Read<float>(punchSvc + 0x48) : 0.f;
            float pitchPunch = punchSvc ? CS2::Read<float>(punchSvc + 0x4C) : 0.f;

            // Delta approach: only apply the change each tick
            static float lastYaw = 0.f, lastPitch = 0.f;
            if (shots <= 0) { lastYaw = 0.f; lastPitch = 0.f; }
            else if (punchSvc) {
                float dYaw   = yawPunch   - lastYaw;
                float dPitch = pitchPunch - lastPitch;
                lastYaw   = yawPunch;
                lastPitch = pitchPunch;

                if (fabsf(dYaw) > 0.0001f || fabsf(dPitch) > 0.0001f) {
                    static bool diagDone = false;
                    if (!diagDone) {
                        diagDone = true;
                        char buf[160];
                        sprintf_s(buf, "NoRecoil: dPitch=%.4f dYaw=%.4f (pitch=%.3f yaw=%.3f)",
                            dPitch, dYaw, pitchPunch, yawPunch);
                        Logger::Log(buf);
                    }
                    Vector3 va = CS2::Read<Vector3>(viewAngAddr);
                    // Pitch: positive punch → view goes UP → to cancel, INCREASE pitch (look down)
                    va.x += dPitch;
                    // Yaw: positive punch → view goes RIGHT → to cancel, DECREASE yaw
                    va.y -= dYaw;
                    if (va.x >  89.f) va.x =  89.f;
                    if (va.x < -89.f) va.x = -89.f;
                    Memory::Write(viewAngAddr, &va, sizeof(va));
                }
            }
        }
    }

    // ----------------------------------------------------------------
    // No flash
    // ----------------------------------------------------------------
    uintptr_t localCtrlAddr = Offsets::Get("dwLocalPlayerController");
    if (!localCtrlAddr) return;
    uintptr_t localCtrl = CS2::Read<uintptr_t>(localCtrlAddr);
    if (!localCtrl) return;
    uintptr_t listAddr   = Offsets::Get("dwEntityList");
    uintptr_t entityList = listAddr ? CS2::Read<uintptr_t>(listAddr) : 0;
    uintptr_t localPawn2 = entityList ? CS2::GetPawn(entityList, localCtrl) : 0;
    if (!localPawn2) return;

    if (cfg->m_noFlash) {
        const struct { const char* n; uintptr_t fb; } ff[] = {
            { "m_flFlashDuration", 0x121C },
            { "m_flFlashMaxAlpha", 0x1218 },
        };
        float zero = 0.f;
        for (auto& f : ff) {
            uintptr_t off = Offsets::Get(f.n, f.fb);
            if (off) Memory::Write(localPawn2 + off, &zero, sizeof(zero));
        }
    }
}

void Misc::DoKnifeBot()       {}
void Misc::DoVoteReveal()     {}
void Misc::DoSkinChanger()    {}
void Misc::DoNameSpammer()    {}
void Misc::DoClanTagSpammer() {}
void Misc::DoAutoAccept()     {}
void Misc::DoRankRevealer()   {}
void Misc::DoDamageReport()   {}
void Misc::DoHUDRemoval()     {}
void Misc::DoSkyboxRemoval()  {}
