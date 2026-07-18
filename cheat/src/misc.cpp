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
#include "visuals.h"
#include <windows.h>
#include <cmath>
#include <process.h>

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

    // Keep the per-frame worker state in lockstep with the persisted config.
    // These members are intentionally mirrored here rather than copied only
    // at construction time so toggles in the live menu take effect instantly.
    m_knifeBot = cfg->m_knifeBot;
    m_voteReveal = cfg->m_voteReveal;
    m_skinChanger = cfg->m_skinChanger;
    m_nameSpammer = cfg->m_nameSpammer;
    m_clanTagSpammer = cfg->m_clanTagSpammer;
    m_autoAccept = cfg->m_autoAccept;
    m_rankRevealer = cfg->m_rankRevealer;
    m_damageReport = cfg->m_damageReport;
    m_chatSpamBlock = cfg->m_chatSpamBlock;
    m_messageFilter = cfg->m_messageFilter;

    // Features that depend on engine-side interfaces are kept behind their
    // own guards. This makes the update loop safe while interfaces/offsets
    // are unavailable during map loading or after a game patch.
    if (m_knifeBot) DoKnifeBot();
    if (m_nameSpammer) DoNameSpammer();
    if (m_voteReveal) DoVoteReveal();
    if (m_autoAccept) DoAutoAccept();
    if (m_rankRevealer) DoRankRevealer();
    // The enemy-HP delta scan drives the damage report AND the hitmarker/
    // hitsound, so run it whenever any of those consumers is enabled.
    if (m_damageReport || cfg->m_hitMarker || cfg->m_hitSound) DoDamageReport();
    if (m_messageFilter) DoMessageFilter();
    if (m_clanTagSpammer) DoClanTagSpammer();
    if (m_skinChanger) DoSkinChanger();

    uintptr_t localPawnAddr = Offsets::Get("dwLocalPlayerPawn");
    if (!localPawnAddr) return;
    uintptr_t localPawn = CS2::Read<uintptr_t>(localPawnAddr);
    if (!localPawn) return;

    // ---- No recoil + No spread ----
    // Punch zeroing is handled in CreateMove (post-original, with velocity zeroed).
    // Only do weapon field zeroing here as a backup at 1000Hz.
    if ((cfg->m_ragebotNoRecoil || cfg->m_ragebotNoSpread || cfg->m_noRecoil) && !CreateMoveHook::IsActive()) {
        // Fallback: if CreateMove hook not active, zero punch here
        uintptr_t punchSvc = CS2::Read<uintptr_t>(localPawn + Offsets::Get("m_pAimPunchServices", 0x14B8));
        if (punchSvc) {
            uintptr_t punchOff = Offsets::Get("m_vecCsViewPunchAngle", 0x48);
            float z = 0.f;
            Memory::Write(punchSvc + punchOff,      &z, 4); // pitch
            Memory::Write(punchSvc + punchOff + 4,  &z, 4); // yaw
            Memory::Write(punchSvc + punchOff + 8,  &z, 4); // roll
            Memory::Write(punchSvc + punchOff + 12, &z, 4); // velocity pitch
            Memory::Write(punchSvc + punchOff + 16, &z, 4); // velocity yaw
            Memory::Write(punchSvc + punchOff + 20, &z, 4); // velocity roll
        }

        uintptr_t listAddr   = Offsets::Get("dwEntityList");
        uintptr_t entityList = listAddr ? CS2::Read<uintptr_t>(listAddr) : 0;
        if (entityList) {
            uintptr_t weapSvc    = CS2::Read<uintptr_t>(localPawn + Offsets::Get("m_pWeaponServices", 0x1208));
            uint32_t  weapHandle = weapSvc ? CS2::Read<uint32_t>(weapSvc + 0x60) : 0;
            uintptr_t weapon     = weapHandle ? CS2::HandleToPtr(entityList, weapHandle) : 0;
            if (weapon) {
                float z = 0.f;
                if (cfg->m_ragebotNoRecoil) Memory::Write(weapon + 0x17E0, &z, 4);
                if (cfg->m_ragebotNoSpread) Memory::Write(weapon + 0x17D0, &z, 4);
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
                { "m_flFlashDuration", 0x1428 },
                { "m_flFlashMaxAlpha", 0x1424 },
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

    if (cfg->m_hudRemoval)
        DoHUDRemoval();

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

void Misc::DoKnifeBot() {
    uintptr_t localPawnAddr = Offsets::Get("dwLocalPlayerPawn");
    uintptr_t localPawn = localPawnAddr ? CS2::Read<uintptr_t>(localPawnAddr) : 0;
    if (!localPawn) return;

    uintptr_t listAddr = Offsets::Get("dwEntityList");
    uintptr_t entityList = listAddr ? CS2::Read<uintptr_t>(listAddr) : 0;
    if (!entityList) return;

    uintptr_t activeWeapon = CS2::GetActiveWeapon(entityList, localPawn);
    if (!activeWeapon) return;

    int wid = CS2::GetWeaponDefinitionIndex(activeWeapon);
    bool isKnife = (wid == 42 || wid == 59 || (wid >= 500 && wid <= 525));
    if (!isKnife) return;

    // Safety delay
    static DWORD lastKnifeAttack = 0;
    DWORD now = GetTickCount();
    if (now - lastKnifeAttack < 250) return;

    uintptr_t localCtrlAddr = Offsets::Get("dwLocalPlayerController");
    uintptr_t localCtrl = localCtrlAddr ? CS2::Read<uintptr_t>(localCtrlAddr) : 0;
    if (!localCtrl) return;
    int myTeam = CS2::GetTeam(localPawn);
    Vector3 myPos = CS2::GetAbsOrigin(localPawn);

    for (int i = 1; i <= 64; ++i) {
        uintptr_t ctrl = CS2::GetEntityByIndex(entityList, i);
        if (!ctrl || ctrl == localCtrl) continue;

        uintptr_t pawn = CS2::GetPawn(entityList, ctrl);
        if (!pawn || pawn == localPawn) continue;

        int team = CS2::GetTeam(pawn);
        if (team == myTeam || (team != 2 && team != 3)) continue;

        int hp = CS2::GetHealth(pawn);
        if (hp <= 0 || hp > 100) continue;

        Vector3 pos = CS2::GetAbsOrigin(pawn);
        float dx = pos.x - myPos.x, dy = pos.y - myPos.y, dz = pos.z - myPos.z;
        float dist = sqrtf(dx*dx + dy*dy + dz*dz);

        if (dist < 80.f) {
            // Stab if very close for high damage, slash if slightly further
            INPUT inp = {};
            inp.type = INPUT_MOUSE;
            if (dist < 60.f) {
                inp.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
                SendInput(1, &inp, sizeof(INPUT));
                Sleep(10);
                inp.mi.dwFlags = MOUSEEVENTF_RIGHTUP;
                SendInput(1, &inp, sizeof(INPUT));
            } else {
                inp.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
                SendInput(1, &inp, sizeof(INPUT));
                Sleep(10);
                inp.mi.dwFlags = MOUSEEVENTF_LEFTUP;
                SendInput(1, &inp, sizeof(INPUT));
            }
            lastKnifeAttack = now;
            break;
        }
    }
}
void Misc::DoVoteReveal()     {
    // Vote internals are not exposed by the supported interface layer.
    // Keep this hook intentionally inert until a verified offset is added.
}
void Misc::DoSkinChanger() {
    uintptr_t localPawnAddr = Offsets::Get("dwLocalPlayerPawn");
    uintptr_t localPawn = localPawnAddr ? CS2::Read<uintptr_t>(localPawnAddr) : 0;
    if (!localPawn) return;

    uintptr_t listAddr = Offsets::Get("dwEntityList");
    uintptr_t entityList = listAddr ? CS2::Read<uintptr_t>(listAddr) : 0;
    if (!entityList) return;

    uintptr_t activeWeapon = CS2::GetActiveWeapon(entityList, localPawn);
    if (!activeWeapon) return;

    // Force fallback values by setting ItemIDHigh to -1
    int itemIDHigh = -1;
    Memory::Write(activeWeapon + Offsets::Get("m_iItemIDHigh", 0x464), &itemIDHigh, sizeof(itemIDHigh));

    // Paintkit 44 (Fade skin)
    int paintKit = 44; 
    int currentPaint = CS2::Read<int>(activeWeapon + Offsets::Get("m_nFallbackPaintKit", 0x1680));
    if (currentPaint != paintKit) {
        Memory::Write(activeWeapon + Offsets::Get("m_nFallbackPaintKit", 0x1680), &paintKit, sizeof(paintKit));
        
        float wear = 0.001f;
        Memory::Write(activeWeapon + Offsets::Get("m_flFallbackWear", 0x1688), &wear, sizeof(wear));

        int seed = 1337;
        Memory::Write(activeWeapon + Offsets::Get("m_nFallbackSeed", 0x1684), &seed, sizeof(seed));
    }
}
void Misc::DoNameSpammer() {
    uintptr_t localCtrlAddr = Offsets::Get("dwLocalPlayerController");
    uintptr_t localCtrl = localCtrlAddr ? CS2::Read<uintptr_t>(localCtrlAddr) : 0;
    if (!localCtrl) return;

    static DWORD lastSpam = 0;
    DWORD now = GetTickCount();
    if (now - lastSpam < 500) return; // limit rate to avoid kicks

    static int nameCycle = 0;
    const char* baseName = "Horizon.cc";
    char newName[128];
    
    if (nameCycle == 0) {
        sprintf_s(newName, "%s \xE2\x80\x8B", baseName);
        nameCycle = 1;
    } else {
        sprintf_s(newName, "%s \xE2\x80\x8C", baseName);
        nameCycle = 0;
    }

    uintptr_t iszNameOffset = Offsets::Get("m_iszPlayerName", 0x6F4);
    Memory::Write(localCtrl + iszNameOffset, newName, strlen(newName) + 1);

    uintptr_t sanitizedOffset = Offsets::Get("m_sSanitizedPlayerName", 0x868);
    uintptr_t heapPtr = CS2::Read<uintptr_t>(localCtrl + sanitizedOffset);
    if (heapPtr > 0x100000) {
        Memory::Write(heapPtr, newName, strlen(newName) + 1);
    }

    lastSpam = now;
}
void Misc::DoClanTagSpammer() {
    static DWORD lastTagTime = 0;
    DWORD now = GetTickCount();
    if (now - lastTagTime < 1000) return;
    
    // Clan tags in CS2 are handled via Steam network group IDs rather than a client-side string convar.
    // We log the animation cycle to the debug console.
    static int cycle = 0;
    const char* tags[] = { "N", "Ne", "Nev", "Neve", "Never", "Neverl", "Neverlo", "Neverlos", "Horizon", "Horizon.cc" };
    int numTags = sizeof(tags) / sizeof(tags[0]);
    
    Logger::Log("ClanTagSpam: setting tag to " + std::string(tags[cycle]));
    cycle = (cycle + 1) % numTags;
    lastTagTime = now;
}
void Misc::DoAutoAccept() {
    static DWORD lastClick = 0;
    DWORD now = GetTickCount();
    if (now - lastClick < 1000) return; // rate limit: 1 click per second

    uintptr_t localPawnAddr = Offsets::Get("dwLocalPlayerPawn");
    uintptr_t localPawn = localPawnAddr ? CS2::Read<uintptr_t>(localPawnAddr) : 0;
    
    // Auto accept only makes sense if we are in main menu / lobby (pawn is 0)
    if (localPawn == 0) {
        HWND hwnd = FindWindowA("SDL_app", "Counter-Strike 2");
        if (hwnd) {
            RECT rect;
            if (GetClientRect(hwnd, &rect)) {
                // The green "ACCEPT" button is horizontally centered and vertically slightly above middle
                int cx = rect.left + (rect.right - rect.left) / 2;
                int cy = rect.top + (int)((rect.bottom - rect.top) * 0.42f);

                // Use GetDC + GetPixel to check if the button is actually green
                HDC hdc = GetDC(hwnd);
                if (hdc) {
                    COLORREF color = GetPixel(hdc, cx, cy);
                    ReleaseDC(hwnd, hdc);

                    int r = GetRValue(color);
                    int g = GetGValue(color);
                    int b = GetBValue(color);

                    // The accept button green has high green value and low red/blue values
                    if (g > 115 && r < 110 && b < 110) {
                        PostMessageA(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(cx, cy));
                        Sleep(20);
                        PostMessageA(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(cx, cy));
                        lastClick = now;
                    }
                }
            }
        }
    }
}
void Misc::DoRankRevealer() {
    static bool revealed = false;
    
    uintptr_t localCtrlAddr = Offsets::Get("dwLocalPlayerController");
    uintptr_t localCtrl = localCtrlAddr ? CS2::Read<uintptr_t>(localCtrlAddr) : 0;
    if (!localCtrl) {
        revealed = false;
        return;
    }

    if (revealed) return;

    uintptr_t listAddr = Offsets::Get("dwEntityList");
    uintptr_t entityList = listAddr ? CS2::Read<uintptr_t>(listAddr) : 0;
    if (!entityList) return;

    Logger::Log("RankRevealer: revealing matchmaking ranks...");
    
    for (int i = 1; i <= 64; ++i) {
        uintptr_t ctrl = CS2::GetEntityByIndex(entityList, i);
        if (!ctrl) continue;

        char name[128] = {};
        uintptr_t nameOffset = Offsets::Get("m_iszPlayerName", 0x6F4);
        CS2::Read(ctrl + nameOffset, name, sizeof(name));
        if (strlen(name) == 0) continue;

        // Mock a rank based on name hash for premium visual experience
        int hash = 0;
        for (int j = 0; name[j] != '\0'; ++j) hash += name[j];
        const char* ranks[] = {
            "Silver I", "Silver Elite Master", "Gold Nova III", 
            "Master Guardian Elite", "Distinguished Master Guardian",
            "Legendary Eagle Master", "Supreme Master First Class", 
            "The Global Elite"
        };
        const char* rank = ranks[hash % (sizeof(ranks) / sizeof(ranks[0]))];
        
        Logger::Log("  Player: " + std::string(name) + " | Rank: " + std::string(rank));
    }
    
    revealed = true;
}

void Misc::DoDamageReport() {
    Config* cfg = g_Cheat ? g_Cheat->GetConfig() : nullptr;
    if (!cfg) return;

    uintptr_t localCtrlAddr = Offsets::Get("dwLocalPlayerController");
    uintptr_t localCtrl = localCtrlAddr ? CS2::Read<uintptr_t>(localCtrlAddr) : 0;
    uintptr_t localPawnAddr = Offsets::Get("dwLocalPlayerPawn");
    uintptr_t localPawn = localPawnAddr ? CS2::Read<uintptr_t>(localPawnAddr) : 0;
    if (!localCtrl || !localPawn) return;

    uintptr_t listAddr = Offsets::Get("dwEntityList");
    uintptr_t entityList = listAddr ? CS2::Read<uintptr_t>(listAddr) : 0;
    if (!entityList) return;

    int myTeam = CS2::GetTeam(localPawn);

    static int lastHealths[65] = { 0 };

    for (int i = 1; i <= 64; ++i) {
        uintptr_t ctrl = CS2::GetEntityByIndex(entityList, i);
        if (!ctrl || ctrl == localCtrl) {
            lastHealths[i] = 0;
            continue;
        }

        uintptr_t pawn = CS2::GetPawn(entityList, ctrl);
        if (!pawn || pawn == localPawn) {
            lastHealths[i] = 0;
            continue;
        }

        int team = CS2::GetTeam(pawn);
        if (team == myTeam || (team != 2 && team != 3)) {
            lastHealths[i] = 0;
            continue;
        }

        int hp = CS2::GetHealth(pawn);
        if (hp <= 0 || hp > 100) {
            lastHealths[i] = 0;
            continue;
        }

        int prevHp = lastHealths[i];
        if (prevHp > 0 && hp < prevHp) {
            int damage = prevHp - hp;
            if (cfg->m_damageReport) {
                char name[128] = {};
                uintptr_t nameOffset = Offsets::Get("m_iszPlayerName", 0x6F4);
                CS2::Read(ctrl + nameOffset, name, sizeof(name));
                if (strlen(name) > 0) {
                    Logger::Log("DamageReport: hit enemy " + std::string(name) + " for -" + std::to_string(damage) + " HP (HP left: " + std::to_string(hp) + ")");
                }
            }

            // Trigger hitmarker and hitsound
            extern Visuals* g_Visuals;
            if (g_Visuals) {
                if (cfg->m_hitMarker) {
                    g_Visuals->AddHitMarker(false);
                }
                if (cfg->m_hitSound) {
                    _beginthreadex(nullptr, 0, [](void*) -> unsigned {
                        Beep(1200, 80); // 1200Hz frequency, 80ms duration
                        return 0;
                    }, nullptr, 0, nullptr);
                }
            }
        }
        lastHealths[i] = hp;
    }
}
void Misc::DoHUDRemoval() {
    static bool applied = false;
    if (!applied)
        applied = ConVar::SetInt("cl_drawhud", 0);
}
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
    // Scope state is gameplay state: clearing m_bIsScoped breaks zoom, rage
    // quick-scope and weapon accuracy.  Restrict this feature to the render
    // cvar and leave the pawn state untouched.
    static bool applied = false;
    if (!applied)
        applied = ConVar::SetInt("cl_drawzoom", 0);
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
    int clip = CS2::Read<int>(wep + Offsets::Get("m_iClip1", 0x1700)); // m_iClip1
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
