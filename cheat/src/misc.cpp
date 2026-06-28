// =================================================================
// misc.cpp - Misc features
// No-recoil: zeroes punch + weapon recoil index + accuracy penalty
// No-spread:  zeroes weapon spread (m_fAccuracyPenalty zeroed each tick)
// No-flash:   zeroes flash duration/alpha
// Bhop:       via CreateMove hook (create_move.cpp)
// =================================================================

#include "misc.h"
#include "create_move.h"
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

    // ---- No recoil + No spread ----
    // Punch zeroing is handled in CreateMove (post-original, with velocity zeroed).
    // Only do weapon field zeroing here as a backup at 1000Hz.
    if (cfg->m_noRecoil && !CreateMoveHook::IsActive()) {
        // Fallback: if CreateMove hook not active, zero punch here
        uintptr_t punchSvc = CS2::Read<uintptr_t>(localPawn + 0x1490);
        if (punchSvc) {
            float z = 0.f;
            Memory::Write(punchSvc + 0x48, &z, 4);
            Memory::Write(punchSvc + 0x4C, &z, 4);
            Memory::Write(punchSvc + 0x50, &z, 4);
            Memory::Write(punchSvc + 0x54, &z, 4); // velocity
            Memory::Write(punchSvc + 0x58, &z, 4);
            Memory::Write(punchSvc + 0x5C, &z, 4);
        }

        uintptr_t listAddr   = Offsets::Get("dwEntityList");
        uintptr_t entityList = listAddr ? CS2::Read<uintptr_t>(listAddr) : 0;
        if (entityList) {
            uintptr_t weapSvc    = CS2::Read<uintptr_t>(localPawn + 0x11E0);
            uint32_t  weapHandle = weapSvc ? CS2::Read<uint32_t>(weapSvc + 0x60) : 0;
            uintptr_t weapon     = weapHandle ? CS2::HandleToPtr(entityList, weapHandle) : 0;
            if (weapon) {
                float z = 0.f;
                Memory::Write(weapon + 0x17E0, &z, 4);
                Memory::Write(weapon + 0x17D0, &z, 4);
            }
        }
    }

    // ---- No flash ----
    if (cfg->m_noFlash) {
        uintptr_t listAddr   = Offsets::Get("dwEntityList");
        uintptr_t entityList = listAddr ? CS2::Read<uintptr_t>(listAddr) : 0;
        uintptr_t localCtrlAddr = Offsets::Get("dwLocalPlayerController");
        uintptr_t localCtrl = localCtrlAddr ? CS2::Read<uintptr_t>(localCtrlAddr) : 0;
        uintptr_t pawn2 = (entityList && localCtrl) ? CS2::GetPawn(entityList, localCtrl) : 0;
        if (pawn2) {
            float z = 0.f;
            const struct { const char* n; uintptr_t fb; } ff[] = {
                { "m_flFlashDuration", 0x121C },
                { "m_flFlashMaxAlpha", 0x1218 },
            };
            for (auto& f : ff) {
                uintptr_t off = Offsets::Get(f.n, f.fb);
                if (off) Memory::Write(pawn2 + off, &z, 4);
            }
        }
    }

    // ---- Third person ----
    // Third-person is now handled inside the CreateMove hook via
    // CCSGOInput + 0x0A51 = bInThirdPerson (Axion-confirmed CS2 approach).
    // The observer services approach does NOT work in CS2 for live play.

    // ---- Scope overlay removal ----
    // Remove the black scope rings so snipers can see clearly without scope overlay.
    // Write 0 to m_bIsScoped (visual only trick — CS2 uses this for overlay drawing).
    // Note: zeroing m_bIsScoped may reduce sniper accuracy server-side; toggle-able.
    if (cfg->m_scopeRemoval) {
        bool scoped = CS2::Read<bool>(localPawn + 0x1C50); // m_bIsScoped confirmed offset
        if (scoped) {
            bool f = false;
            Memory::Write(localPawn + 0x1C50, &f, 1);
        }
    }

    // ---- Auto-strafe ----
    if (cfg->m_autoStrafe) {
        // Check if player is in the air and moving mouse horizontally
        uint32_t flags = CS2::Read<uint32_t>(localPawn + 0x3F8);
        bool inAir = !(flags & 1);
        if (inAir) {
            POINT cur; GetCursorPos(&cur);
            static POINT last = cur;
            int dx = cur.x - last.x;
            last = cur;
            // Apply strafer: if mouse moving right, strafe right (+D), and vice versa
            // This is handled through view angle manipulation to create circle-strafe
            // Actual strafe needs CreateMove cmd->flSideMove
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
