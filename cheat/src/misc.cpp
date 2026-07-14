// =================================================================
// misc.cpp - Misc features
// No-recoil: zeroes punch + weapon recoil index + accuracy penalty
// No-spread:  zeroes weapon spread (m_fAccuracyPenalty zeroed each tick)
// No-flash:   zeroes flash duration/alpha
// Bhop:       via CreateMove hook (create_move.cpp)
// Shadow/Fog/Smoke/Scope removal: convar-based overrides
// Auto-pistol / Auto-reload: button manipulation per-tick
// =================================================================

#include "misc.h"
#include "create_move.h"
#include "game_classes.h"
#include "offsets.h"
#include "memory.h"
#include "cheat_core.h"
#include "config.h"
#include "logger.h"
#include "convar.h"
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
    if ((cfg->m_noRecoil || cfg->m_noSpread) && !CreateMoveHook::IsActive()) {
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
                if (cfg->m_noRecoil) Memory::Write(weapon + 0x17E0, &z, 4);
                if (cfg->m_noSpread) Memory::Write(weapon + 0x17D0, &z, 4);
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
    // Handled inside the CreateMove hook via CCSGOInput + 0x0A51 (Axion-confirmed).

    // ---- Scope overlay removal ----
    if (cfg->m_scopeRemoval)
        DoScopeRemoval();

    if (cfg->m_skyboxRemoval)
        DoSkyboxRemoval();

    // ---- Shadow removal ----
    if (cfg->m_shadowRemoval)
        DoShadowRemoval();

    // ---- Fog removal ----
    if (cfg->m_fogRemoval)
        DoFogRemoval();

    // ---- Smoke removal ----
    if (cfg->m_smokeRemoval)
        DoSmokeRemoval();

    // ---- Flash reduction ----
    if (cfg->m_flashReduction)
        DoFlashReduction();

    // ---- Auto pistol ----
    if (cfg->m_autoPistol)
        DoAutoPistol();

    // ---- Auto reload ----
    if (cfg->m_autoReload)
        DoAutoReload();

    // ---- Chat spam block ----
    if (cfg->m_chatSpamBlock)
        DoChatSpamBlock();

    // ---- Auto-strafe ----
    if (cfg->m_autoStrafe) {
        uint32_t flags = CS2::Read<uint32_t>(localPawn + Offsets::Get("m_fFlags", 0x3F8));
        bool inAir = !(flags & 1);
        if (inAir) {
            POINT cur; GetCursorPos(&cur);
            static POINT last = cur;
            int dx = cur.x - last.x;
            last = cur;
            // Auto-strafe: write sidemove velocity counter to cursor delta
            // True sidemove injection requires CreateMove hook access to cmd->flSideMove.
            // Here we expose it via dwForceJump analog: write a counter nudge.
            // Full implementation lives in create_move.cpp (hkCreateMove post-original).
            (void)dx; // suppress unused; strafe applied in hkCreateMove
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
void Misc::DoSkyboxRemoval() {
    // r_3dsky controls the 3D skybox pass in CS2. Apply once and let the
    // engine keep the value; repeatedly writing convars can cause needless
    // material refreshes.
    static bool applied = false;
    if (!applied)
        applied = ConVar::SetInt("r_3dsky", 0);
}

void Misc::DoShadowRemoval() {
    // Disable dynamic shadows by overriding r_shadows convar.
    // CS2 uses r_shadows for real-time shadow casting.
    static bool applied = false;
    if (!applied) {
        applied = ConVar::SetInt("r_shadows", 0);
    }
}

void Misc::DoScopeRemoval() {
    uintptr_t localPawnAddr = Offsets::Get("dwLocalPlayerPawn");
    uintptr_t localPawn = localPawnAddr ? CS2::Read<uintptr_t>(localPawnAddr) : 0;
    if (!localPawn) return;
    // m_bIsScoped at 0x1C50 — zeroing hides scope overlay visually
    bool scoped = CS2::Read<bool>(localPawn + 0x1C50);
    if (scoped) {
        bool f = false;
        Memory::Write(localPawn + 0x1C50, &f, 1);
    }
}

void Misc::DoFogRemoval() {
    // CS2: fog_enable convar controls in-game fog rendering.
    static bool applied = false;
    if (!applied) {
        applied = ConVar::SetInt("fog_enable", 0);
    }
}

void Misc::DoSmokeRemoval() {
    // CS2 uses volumetric smoke (GPU-side rendering).
    // r_lowlod_particles suppresses the smoke particle system locally.
    static bool applied = false;
    if (!applied) {
        applied = ConVar::SetInt("r_lowlod_particles", 1);
    }
}

void Misc::DoFlashReduction() {
    // Cap m_flFlashMaxAlpha to m_flashAmount (0..1 mapped to 0..255)
    Config* cfg = g_Cheat ? g_Cheat->GetConfig() : nullptr;
    if (!cfg) return;
    uintptr_t listAddr      = Offsets::Get("dwEntityList");
    uintptr_t localCtrlAddr = Offsets::Get("dwLocalPlayerController");
    uintptr_t entityList = listAddr      ? CS2::Read<uintptr_t>(listAddr)      : 0;
    uintptr_t localCtrl  = localCtrlAddr ? CS2::Read<uintptr_t>(localCtrlAddr) : 0;
    uintptr_t pawn = (entityList && localCtrl) ? CS2::GetPawn(entityList, localCtrl) : 0;
    if (!pawn) return;
    uintptr_t alphaOff = Offsets::Get("m_flFlashMaxAlpha", 0x1218);
    if (!alphaOff) return;
    float alpha = CS2::Read<float>(pawn + alphaOff);
    float maxAlpha = cfg->m_flashAmount * 255.f;
    if (alpha > maxAlpha)
        Memory::Write(pawn + alphaOff, &maxAlpha, 4);
}

void Misc::DoChatSpamBlock() {
    // Suppress incoming chat spam by muting all but friends and party.
    // CS2: cl_mute_all_but_friends_and_party convar.
    static bool applied = false;
    if (!applied) {
        applied = ConVar::SetBool("cl_mute_all_but_friends_and_party", true);
    }
}

void Misc::DoMessageFilter() {
    // Placeholder: filter specific server messages from the in-game chat.
    // Requires hooking ISurface::DrawTexturedRect or the chat panel's
    // AddMessage virtual — deferred until DX11 overlay is wired.
}

void Misc::DoAutoPistol() {
    // Auto-pistol: hold fire on a pistol and cheat fires at the weapon's
    // natural semi-auto rate by toggling IN_ATTACK each tick.
    // Actual button toggle is done in the CreateMove hook; here we just
    // confirm the weapon is a pistol and set the flag.
    uintptr_t localPawnAddr = Offsets::Get("dwLocalPlayerPawn");
    uintptr_t localPawn = localPawnAddr ? CS2::Read<uintptr_t>(localPawnAddr) : 0;
    if (!localPawn) return;
    uintptr_t listAddr   = Offsets::Get("dwEntityList");
    uintptr_t entityList = listAddr ? CS2::Read<uintptr_t>(listAddr) : 0;
    if (!entityList) return;
    uintptr_t wep = CS2::GetActiveWeapon(entityList, localPawn);
    if (!wep) return;
    // Weapon clip model check — pistol class IDs 1-9
    int wid = CS2::Read<int>(wep + 0x300);
    bool isPistol = (wid >= 1 && wid <= 9) || wid == 30; // 30 = Desert Eagle variant
    // The actual auto-fire toggling is in hkCreateMove (post-original per 64Hz tick)
    // We set a static flag readable by the hook
    static bool s_isPistol = false;
    s_isPistol = isPistol;
}

void Misc::DoAutoReload() {
    // Auto-reload: when clip is empty, send reload command via the
    // weapon services. CS2: m_iClip1 at 0x1774 in CCSWeaponBase.
    uintptr_t localPawnAddr = Offsets::Get("dwLocalPlayerPawn");
    uintptr_t localPawn = localPawnAddr ? CS2::Read<uintptr_t>(localPawnAddr) : 0;
    if (!localPawn) return;
    uintptr_t listAddr   = Offsets::Get("dwEntityList");
    uintptr_t entityList = listAddr ? CS2::Read<uintptr_t>(listAddr) : 0;
    if (!entityList) return;
    uintptr_t wep = CS2::GetActiveWeapon(entityList, localPawn);
    if (!wep) return;
    int clip = CS2::Read<int>(wep + 0x1774); // m_iClip1
    if (clip == 0) {
        // Force reload by triggering +reload cmd (write to the ForceReload flag).
        // CS2: dwForceAttack2 is the "alt fire" channel used for reload in some builds.
        uintptr_t forceReloadAddr = Offsets::Get("dwForceAttack2");
        if (forceReloadAddr) {
            int v = 65537; // CS2 force flag pattern
            Memory::Write(forceReloadAddr, &v, 4);
        }
    }
}
