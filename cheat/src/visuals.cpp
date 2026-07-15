// =================================================================
// visuals.cpp - CS2 ESP / Visuals
//
// Uses the correct CS2 entity system:
//   Entity list: chunk-based, CS2::GetEntityByIndex
//   Pawn from controller: CS2::GetPawn
//   Bone positions: CS2::GetBoneArray / CS2::GetBonePos (stride 32)
//   World-to-screen: CS2::W2S
//
// CS2 player model bone hierarchy (T/CT standard model):
//   Spine chain:  0→1→2→3→4→5  (pelvis→spine1→spine2→spine3→neck→head)
//   Left arm:  3→6→7→8→9     (spine3→clavicle_L→upper_L→lower_L→hand_L)
//   Right arm: 3→10→11→12→13 (spine3→clavicle_R→upper_R→lower_R→hand_R)
//   Left leg:  0→14→15→16    (pelvis→hip_L→knee_L→ankle_L)
//   Right leg: 0→18→19→20    (pelvis→hip_R→knee_R→ankle_R)
// =================================================================

#include "visuals.h"
#include "memory.h"
#include "offsets.h"
#include "logger.h"
#include "config.h"
#include "ui_manager.h"
#include "cheat_core.h"
#include "no_spread.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>

Visuals* g_Visuals = nullptr;

// ---- CS2 bone connections (parent → child pairs) ----
static const std::pair<int,int> kBoneLinks[] = {
    // Spine
    {6,5},{5,4},{4,2},{2,0},
    // Left arm
    {5,8},{8,9},{9,10},
    // Right arm
    {5,13},{13,14},{14,15},
    // Left leg
    {0,22},{22,23},{23,24},
    // Right leg
    {0,25},{25,26},{26,27},
};
static const int kNumBoneLinks = (int)(sizeof(kBoneLinks)/sizeof(kBoneLinks[0]));
// Highest bone index we use: CS2 bones go up to index 27 (feet)
static const int kMaxBone = 28;

Visuals::Visuals()
    : m_enabled(false), m_espEnabled(false), m_espBox(false)
    , m_espHealthBar(false), m_espArmorBar(false), m_espName(false)
    , m_espWeapon(false), m_espFlags(false), m_espSkeleton(false)
    , m_espSnaplines(false), m_espDistance(false), m_espSound(false)
    , m_espTeammates(false)
    , m_espVisibleColor(0,255,0,255), m_espHiddenColor(255,100,100,255)
    , m_chamsEnabled(false), m_chamsVisible(false)
    , m_chamsVisibleColor(0,255,0,200), m_chamsHidden(false)
    , m_chamsHiddenColor(255,0,0,200), m_chamsWeapon(false)
    , m_chamsWeaponColor(0,150,255,200), m_chamsVisibleMode(0), m_chamsHiddenMode(1)
    , m_glowEnabled(false), m_glowColor(0,128,255,255), m_glowAlpha(0.5f), m_glowHidden(false)
    , m_hitMarker(false), m_hitMarkerTime(0.5f)
    , m_grenadePrediction(false), m_bombTimer(false), m_defuseTimer(false)
    , m_damageIndicator(false), m_radar(false), m_spectatorList(false)
    , m_killFeed(false), m_hitSound(false), m_headshotSound(false), m_soundVolume(50.f)
{}

// ---- helpers ----
static float Dist3D(const Vector3& a, const Vector3& b) {
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return sqrtf(dx * dx + dy * dy + dz * dz);
}
static ImU32 ToImU32(ImColor c) { return ImGui::ColorConvertFloat4ToU32(c); }

static std::string GetWeaponNameByID(int id) {
    switch(id) {
        case 1:  return "Deagle";    case 2:  return "Dualies";
        case 3:  return "Five-7";    case 4:  return "Glock";
        case 7:  return "P250";      case 9:  return "CZ75";
        case 10: return "R8";        case 11: return "AWP";
        case 12: return "SSG08";     case 13: return "SCAR-20";
        case 14: return "G3SG1";     case 16: return "AK-47";
        case 17: return "M4A4";      case 60: return "M4A1-S";
        case 19: return "FAMAS";     case 18: return "Galil";
        case 23: return "SG553";     case 24: return "AUG";
        case 25: return "M249";      case 28: return "Negev";
        case 29: return "Mag-7";     case 31: return "Nova";
        case 35: return "XM1014";    case 34: return "Sawed-Off";
        case 32: return "MP9";       case 33: return "MP7";
        case 26: return "UMP-45";    case 30: return "P90";
        case 36: return "Flashbang"; case 37: return "Smoke";
        case 38: return "HE";        case 39: return "Molotov";
        case 40: return "Incendiary";case 42: return "C4";
        default: return "";
    }
}

// ---- per-player struct ----
struct PlayerInfo {
    uintptr_t pawn;
    uintptr_t ctrl;
    int  hp;
    int  armor;
    int  team;
    bool isEnemy;
    bool scoped;
    Vector3 origin;   // abs world origin (feet)
    Vector3 head;     // head bone world pos
    Vector2 scrFeet;
    Vector2 scrHead;
    Vector2 scrTop;   // slightly above head for box
    float   height;   // screen height
    float   width;    // screen box width
    std::string name;
    std::string weapon;
    Vector3 bones[kMaxBone+1]; // world positions of bones 0..kMaxBone
    bool bonesValid;
};

void Visuals::Render() {
    Config* cfg = g_Cheat ? g_Cheat->GetConfig() : nullptr;
    if (!cfg || !cfg->m_visualsEnabled) return;

    // Indicators are independent of ESP. In particular, disabling the ESP
    // panel must not suppress hitmarkers.
    if (cfg->m_hitMarker) RenderHitMarkers();
    if (cfg->m_grenadePrediction) RenderGrenadePrediction();
    if (!cfg->m_espEnabled) return;

    uintptr_t lcAddr   = Offsets::Get("dwLocalPlayerController");
    uintptr_t lpAddr   = Offsets::Get("dwLocalPlayerPawn");
    uintptr_t listAddr = Offsets::Get("dwEntityList");
    if (!lcAddr || !lpAddr || !listAddr) return;

    uintptr_t localCtrl = CS2::Read<uintptr_t>(lcAddr);
    uintptr_t localPawn = CS2::Read<uintptr_t>(lpAddr);
    uintptr_t entityList = CS2::Read<uintptr_t>(listAddr);
    if (!localCtrl || !localPawn || !entityList) return;

    int localTeam = CS2::GetTeam(localPawn);
    Vector3 localOrigin = CS2::GetAbsOrigin(localPawn);

    Matrix4x4 vm = CS2::GetViewMatrix();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const int screenW = display.x > 1.0f ? static_cast<int>(display.x) : 1920;
    const int screenH = display.y > 1.0f ? static_cast<int>(display.y) : 1080;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    // ---- Enumerate all player controllers (indices 1..64) ----
    for (int i = 1; i <= 64; ++i) {
        uintptr_t ctrl = CS2::GetEntityByIndex(entityList, i);
        if (!ctrl || ctrl == localCtrl) continue;

        uintptr_t pawn = CS2::GetPawn(entityList, ctrl);
        if (!pawn || pawn == localPawn) continue;

        int hp = CS2::GetHealth(pawn);
        if (hp <= 0 || hp > 100) continue;
        if (CS2::GetLife(pawn) != 0) continue; // alive = 0

        // Team membership is stored on the pawn for current CS2 builds;
        // controller values may be unset for bots and deathmatch players.
        int team = CS2::GetTeam(pawn);
        if (team != 2 && team != 3) continue;
        bool isEnemy = (team != localTeam);
        if (!isEnemy && !cfg->m_espTeammates) continue;

        Vector3 origin = CS2::GetAbsOrigin(pawn);
        if (!std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z)) continue;
        if (origin.x == 0.f && origin.y == 0.f && origin.z == 0.f) continue;

        // ---- Bone positions ----
        PlayerInfo pi{};
        pi.pawn    = pawn;
        pi.ctrl    = ctrl;
        pi.hp      = hp;
        pi.team    = team;
        pi.isEnemy = isEnemy;
        pi.origin  = origin;
        pi.armor   = CS2::Read<int>(pawn + Offsets::Get("m_ArmorValue", 0xEB0));
        pi.scoped  = CS2::Read<bool>(pawn + Offsets::Get("m_bIsScoped", 0x1C70));
        pi.bonesValid = false;

        uintptr_t boneArr = CS2::GetBoneArray(pawn);
        Vector3 headPos = { origin.x, origin.y, origin.z + 72.f }; // fallback
        if (boneArr) {
            pi.bonesValid = true;
            for (int b = 0; b <= kMaxBone; ++b)
                pi.bones[b] = CS2::GetBonePos(boneArr, b);
            // Bone 6 = head in standard CS2 model
            if (std::isfinite(pi.bones[6].x) && std::isfinite(pi.bones[6].y) &&
                std::isfinite(pi.bones[6].z) &&
                (pi.bones[6].x != 0.f || pi.bones[6].y != 0.f || pi.bones[6].z != 0.f))
                headPos = pi.bones[6];
        }
        pi.head = headPos;

        // ---- World to screen ----
        Vector3 topPos = { headPos.x, headPos.y, headPos.z + 8.f };
        Vector2 scrFeet, scrHead, scrTop;
        if (!Utils::WorldToScreen(origin,  scrFeet, vm, screenW, screenH)) continue;
        if (!Utils::WorldToScreen(headPos, scrHead, vm, screenW, screenH)) continue;
        if (!Utils::WorldToScreen(topPos, scrTop, vm, screenW, screenH)) continue;

        float h = scrFeet.y - scrTop.y;
        if (h < 5.f) continue; // offscreen / too small

        float w = h * 0.4f;
        pi.scrFeet  = scrFeet;
        pi.scrHead  = scrHead;
        pi.scrTop   = scrTop;
        pi.height   = h;
        pi.width    = w;

        // Names / weapon
        pi.name   = CS2::GetName(ctrl);
        if (pi.name.empty()) pi.name = "Player";

        // Weapon from weapon services
        uintptr_t weapSvc = CS2::Read<uintptr_t>(pawn + 0x11E0);
        if (weapSvc) {
            uint32_t wh = CS2::Read<uint32_t>(weapSvc + 0x60);
            uintptr_t weap = wh ? CS2::HandleToPtr(entityList, wh) : 0;
            if (weap) {
                int wid = CS2::Read<int>(weap + 0x300); // m_nSubType / weapon id approx
                pi.weapon = GetWeaponNameByID(wid);
            }
        }

        // ---- Colors ----
        ImColor col   = pi.isEnemy ? cfg->m_espHiddenColor : ImColor(100,200,255,255);
        ImU32   col32 = ToImU32(col);

        float x0 = scrTop.x - w/2.f;
        float y0 = scrTop.y;
        float x1 = scrTop.x + w/2.f;
        float y1 = scrFeet.y;

        // Chams/glow are rendered from the projected player bounds.  The old
        // bone-capsule approximation expanded by world-space radii, which
        // made it explode at close range and disappear at long range.
        {
            bool isVisible = false;
            if (NoSpread::IsReady()) {
                Vector3 localEye = { localOrigin.x, localOrigin.y, localOrigin.z + 64.f };
                isVisible = NoSpread::TraceLine(localPawn, pi.pawn, localEye, pi.head) ||
                            NoSpread::TraceLine(localPawn, pi.pawn, localEye, pi.origin);
            } else {
                isVisible = CS2::Read<bool>(pi.pawn + 0x1340) || (Dist3D(localOrigin, pi.origin) < 1500.f);
            }

            bool drawChams = false;
            ImColor chamsCol;
            if (isVisible) {
                if (cfg->m_chamsEnabled && cfg->m_chamsVisible) {
                    drawChams = true;
                    chamsCol = cfg->m_chamsVisibleColor;
                }
            } else {
                if (cfg->m_chamsEnabled && cfg->m_chamsHidden) {
                    drawChams = true;
                    chamsCol = cfg->m_chamsHiddenColor;
                }
            }

            // ---- 1. GLOW LAYER ----
            if (cfg->m_glowEnabled) {
                ImVec4 gv = cfg->m_glowColor.Value;
                float alpha = std::clamp(cfg->m_glowAlpha, 0.f, 1.f);
                ImU32 outer = ImGui::ColorConvertFloat4ToU32(ImVec4(gv.x, gv.y, gv.z, alpha * .22f));
                ImU32 inner = ImGui::ColorConvertFloat4ToU32(ImVec4(gv.x, gv.y, gv.z, alpha * .55f));
                dl->AddRect(ImVec2(x0 - 5.f, y0 - 5.f), ImVec2(x1 + 5.f, y1 + 5.f), outer, 0.f, 0, 5.f);
                dl->AddRect(ImVec2(x0 - 2.f, y0 - 2.f), ImVec2(x1 + 2.f, y1 + 2.f), inner, 0.f, 0, 2.f);
            }

            // ---- 2. MAIN CHAMS LAYER ----
            if (drawChams) {
                ImU32 colorU32 = ToImU32(chamsCol);
                ImVec4 fill = chamsCol.Value;
                fill.w *= .32f;
                dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), ImGui::ColorConvertFloat4ToU32(fill));
                dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), colorU32, 0.f, 0, 1.5f);
            }
        }

        // Helper for drawing text with a dropshadow
        auto drawTextWithShadow = [&](ImVec2 pos, ImU32 color, const char* text) {
            dl->AddText(ImVec2(pos.x + 1.f, pos.y + 1.f), IM_COL32(0, 0, 0, 220), text);
            dl->AddText(pos, color, text);
        };

        // ---- Box ----
        if (cfg->m_espBox) {
            float cw = w * 0.25f;
            float ch = h * 0.2f;
            
            // Helper for drawing corner line with dropshadow
            auto drawCornerLine = [&](ImVec2 p1, ImVec2 p2) {
                // Drop shadow
                dl->AddLine(ImVec2(p1.x - 1.f, p1.y), ImVec2(p2.x - 1.f, p2.y), IM_COL32(0, 0, 0, 200), 2.5f);
                dl->AddLine(ImVec2(p1.x + 1.f, p1.y), ImVec2(p2.x + 1.f, p2.y), IM_COL32(0, 0, 0, 200), 2.5f);
                dl->AddLine(ImVec2(p1.x, p1.y - 1.f), ImVec2(p2.x, p2.y - 1.f), IM_COL32(0, 0, 0, 200), 2.5f);
                dl->AddLine(ImVec2(p1.x, p1.y + 1.f), ImVec2(p2.x, p2.y + 1.f), IM_COL32(0, 0, 0, 200), 2.5f);
                // Main neon cyan/green line
                dl->AddLine(p1, p2, col32, 1.5f);
            };

            // Top-Left
            drawCornerLine(ImVec2(x0, y0), ImVec2(x0 + cw, y0));
            drawCornerLine(ImVec2(x0, y0), ImVec2(x0, y0 + ch));

            // Top-Right
            drawCornerLine(ImVec2(x1, y0), ImVec2(x1 - cw, y0));
            drawCornerLine(ImVec2(x1, y0), ImVec2(x1, y0 + ch));

            // Bottom-Left
            drawCornerLine(ImVec2(x0, y1), ImVec2(x0 + cw, y1));
            drawCornerLine(ImVec2(x0, y1), ImVec2(x0, y1 - ch));

            // Bottom-Right
            drawCornerLine(ImVec2(x1, y1), ImVec2(x1 - cw, y1));
            drawCornerLine(ImVec2(x1, y1), ImVec2(x1, y1 - ch));
        }

        // ---- Health bar (left side) ----
        if (cfg->m_espHealthBar) {
            float pct   = hp / 100.f;
            float barH  = h * pct;
            float bx    = x0 - 6.f;
            
            // Draw background bar
            dl->AddRectFilled(ImVec2(bx - 2, y0), ImVec2(bx + 1, y1), IM_COL32(10, 12, 16, 200));
            
            // Health color logic: green -> yellow -> red vertical gradient
            float r = pct > 0.5f ? 2.0f * (1.f - pct) : 1.0f;
            float g = pct > 0.5f ? 1.0f : 2.0f * pct;
            ImU32 topCol = IM_COL32((int)(r * 255.f), (int)(g * 255.f), 40, 255);
            ImU32 botCol = IM_COL32(220, 50, 50, 255);
            
            dl->AddRectFilledMultiColor(
                ImVec2(bx - 2, y1 - barH), // Min (Top-Left)
                ImVec2(bx + 1, y1),        // Max (Bottom-Right)
                topCol, topCol, botCol, botCol
            );
        }

        // ---- Armor bar (right side) ----
        if (cfg->m_espArmorBar && pi.armor > 0) {
            float pct   = pi.armor / 100.f;
            float barH  = h * pct;
            float bx    = x1 + 4.f;
            dl->AddRectFilled(ImVec2(bx, y0), ImVec2(bx + 3, y1), IM_COL32(10, 12, 16, 200));
            dl->AddRectFilled(ImVec2(bx, y1 - barH), ImVec2(bx + 3, y1), IM_COL32(0, 162, 255, 255));
        }

        // ---- Name ----
        if (cfg->m_espName) {
            ImVec2 ts = ImGui::CalcTextSize(pi.name.c_str());
            drawTextWithShadow(ImVec2(scrTop.x - ts.x * 0.5f, y0 - ts.y - 2), col32, pi.name.c_str());
        }

        // ---- Weapon ----
        if (cfg->m_espWeapon && !pi.weapon.empty()) {
            ImVec2 ts = ImGui::CalcTextSize(pi.weapon.c_str());
            drawTextWithShadow(ImVec2(scrTop.x - ts.x * 0.5f, y1 + 2), IM_COL32(230, 230, 230, 255), pi.weapon.c_str());
        }

        // ---- Distance ----
        if (cfg->m_espDistance) {
            float dx = origin.x - localOrigin.x, dy = origin.y - localOrigin.y, dz = origin.z - localOrigin.z;
            int dist = (int)(sqrtf(dx * dx + dy * dy + dz * dz) * 0.0254f);
            char buf[16]; sprintf_s(buf, "%dm", dist);
            ImVec2 ts = ImGui::CalcTextSize(buf);
            float textY = y1 + (cfg->m_espWeapon && !pi.weapon.empty() ? 14.f : 2.f);
            drawTextWithShadow(ImVec2(scrTop.x - ts.x * 0.5f, textY), IM_COL32(200, 200, 200, 220), buf);
        }

        // ---- Flags ----
        if (cfg->m_espFlags) {
            float fy = y0;
            auto addFlag = [&](const char* txt, ImU32 c) {
                drawTextWithShadow(ImVec2(x1 + (cfg->m_espArmorBar && pi.armor > 0 ? 10.f : 6.f), fy), c, txt);
                fy += 13.f;
            };
            if (pi.scoped) addFlag("SCOPED", IM_COL32(0, 220, 255, 255));
            uint32_t flags = CS2::Read<uint32_t>(pawn + Offsets::Get("m_fFlags", 0x3F8));
            if (!(flags & 1)) addFlag("AIR", IM_COL32(255, 220, 80, 255));
            bool reloading = CS2::Read<bool>(pawn + 0x1468);
            if (reloading) addFlag("RELOAD", IM_COL32(255, 140, 0, 255));
        }

        // ---- Skeleton ----
        if (cfg->m_espSkeleton && pi.bonesValid) {
            ImU32 skCol = IM_COL32(0,255,80,200);
            for (int b = 0; b < kNumBoneLinks; ++b) {
                int pa = kBoneLinks[b].first;
                int ch = kBoneLinks[b].second;
                if (pa > kMaxBone || ch > kMaxBone) continue;
                Vector3& wp = pi.bones[pa];
                Vector3& wc = pi.bones[ch];
                if ((wp.x == 0.f && wp.y == 0.f) || (wc.x == 0.f && wc.y == 0.f)) continue;
                Vector2 sp, sc;
                if (!Utils::WorldToScreen(wp, sp, vm, screenW, screenH)) continue;
                if (!Utils::WorldToScreen(wc, sc, vm, screenW, screenH)) continue;
                dl->AddLine(ImVec2(sp.x,sp.y), ImVec2(sc.x,sc.y), skCol, 1.f);
            }
        }

        // ---- Snaplines ----
        if (cfg->m_espSnaplines) {
            float cx = ImGui::GetIO().DisplaySize.x * 0.5f;
            float cy = ImGui::GetIO().DisplaySize.y;
            dl->AddLine(ImVec2(cx,cy), ImVec2(scrFeet.x,scrFeet.y), col32, 1.f);
        }
    }

    // ---- Hit markers ----
    // ---- Bomb timer ----
    if (cfg->m_bombTimer) RenderBombTimer();

    // ---- Spectator list ----
    if (cfg->m_spectatorList) RenderSpectatorList();

    // ---- Radar ----
    if (cfg->m_radar) RenderRadar();

    // ---- Sleek HUD Indicators ----
    RenderIndicators();
}

void Visuals::RenderHitMarkers() {
    static std::vector<HitMarker> s_markers;
    for (auto& m : m_hitMarkers) s_markers.push_back(m);
    m_hitMarkers.clear();

    float cx = ImGui::GetIO().DisplaySize.x * 0.5f;
    float cy = ImGui::GetIO().DisplaySize.y * 0.5f;
    float sz = 8.f;
    DWORD now = GetTickCount();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    s_markers.erase(
        std::remove_if(s_markers.begin(), s_markers.end(), [&](const HitMarker& m){
            float elapsed = (float)(now - m.time) / 1000.f;
            float alpha   = 1.f - elapsed / (m_hitMarkerTime > 0.01f ? m_hitMarkerTime : 0.5f);
            if (alpha <= 0.f) return true;
            ImU32 c = m.headshot ? IM_COL32(255,50,50,(int)(255*alpha))
                                 : IM_COL32(255,255,255,(int)(255*alpha));
            // Cross hitmarker
            dl->AddLine(ImVec2(cx-sz, cy-sz), ImVec2(cx-sz/3, cy-sz/3), c, 1.5f);
            dl->AddLine(ImVec2(cx+sz/3, cy-sz/3), ImVec2(cx+sz, cy-sz), c, 1.5f);
            dl->AddLine(ImVec2(cx-sz, cy+sz), ImVec2(cx-sz/3, cy+sz/3), c, 1.5f);
            dl->AddLine(ImVec2(cx+sz/3, cy+sz/3), ImVec2(cx+sz, cy+sz), c, 1.5f);
            return false;
        }),
        s_markers.end()
    );
}

void Visuals::RenderGrenadePrediction() {
    uintptr_t lpAddr = Offsets::Get("dwLocalPlayerPawn");
    uintptr_t listAddr = Offsets::Get("dwEntityList");
    uintptr_t vaAddr = Offsets::Get("dwViewAngles");
    if (!lpAddr || !listAddr || !vaAddr) return;

    uintptr_t pawn = CS2::Read<uintptr_t>(lpAddr);
    uintptr_t list = CS2::Read<uintptr_t>(listAddr);
    if (!pawn || !list) return;
    uintptr_t weapon = CS2::GetActiveWeapon(list, pawn);
    if (!weapon) return;

    int weaponId = CS2::Read<int>(weapon + 0x300);
    if (weaponId < 43 || weaponId > 48) return;

    Vector3 origin = CS2::GetAbsOrigin(pawn);
    Vector3 ang = CS2::Read<Vector3>(vaAddr);
    const float pitch = ang.x * (float)M_PI / 180.f;
    const float yaw = ang.y * (float)M_PI / 180.f;
    Vector3 pos(origin.x, origin.y, origin.z + 64.f);
    Vector3 vel(cosf(pitch) * cosf(yaw) * 750.f,
                cosf(pitch) * sinf(yaw) * 750.f,
                -sinf(pitch) * 750.f);

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    Matrix4x4 vm = CS2::GetViewMatrix();
    ImVec2 display = ImGui::GetIO().DisplaySize;
    int sw = display.x > 1.f ? (int)display.x : 1920;
    int sh = display.y > 1.f ? (int)display.y : 1080;
    Vector2 previous{};
    bool havePrevious = false;
    constexpr float dt = 1.f / 32.f;
    for (int i = 0; i < 96; ++i) {
        Vector3 next(pos.x + vel.x * dt,
                     pos.y + vel.y * dt,
                     pos.z + vel.z * dt - 0.5f * 800.f * dt * dt);
        vel.z -= 800.f * dt;
        Vector2 screen{};
        if (Utils::WorldToScreen(next, screen, vm, sw, sh)) {
            if (havePrevious)
                dl->AddLine(ImVec2(previous.x, previous.y), ImVec2(screen.x, screen.y),
                            IM_COL32(255, 210, 80, 220), 2.f);
            previous = screen;
            havePrevious = true;
        } else {
            havePrevious = false;
        }
        pos = next;
        if (pos.z < origin.z) break;
    }
}

void Visuals::RenderBombTimer() {
    uintptr_t listAddr = Offsets::Get("dwEntityList");
    if (!listAddr) return;
    uintptr_t entityList = CS2::Read<uintptr_t>(listAddr);
    if (!entityList) return;

    // Scan entities 64 to 2048 for planted C4
    for (int i = 64; i < 2048; ++i) {
        uintptr_t entity = CS2::GetEntityByIndex(entityList, i);
        if (!entity) continue;

        // Try reading CPlantedC4 fields (m_bBombTicking offset is usually 0xFC0 or 0xFD4, timer is 0xFC4 or 0xFD8)
        // Let's use known signature pattern offsets: ticking = 0xF40 / 0xFC0, timer = 0xF44 / 0xFC4
        bool ticking = CS2::Read<bool>(entity + 0xFC0);
        if (!ticking) ticking = CS2::Read<bool>(entity + 0xF40); // backup offset
        if (!ticking) continue;

        float timer = CS2::Read<float>(entity + 0xFC4);
        if (timer <= 0.f || timer > 45.f) timer = CS2::Read<float>(entity + 0xF44); // backup offset
        if (timer <= 0.f || timer > 45.f) continue;

        // Render bomb timer bar
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        float screenW = ImGui::GetIO().DisplaySize.x;
        float barW = 300.f;
        float barH = 15.f;
        float x = (screenW - barW) * 0.5f;
        float y = 80.f;

        float progress = timer / 40.f; // 40 second bomb timer
        if (progress > 1.f) progress = 1.f;
        if (progress < 0.f) progress = 0.f;

        ImU32 bgCol = IM_COL32(0, 0, 0, 180);
        ImU32 progressCol = progress > 0.25f ? IM_COL32(0, 220, 0, 255) : IM_COL32(220, 50, 50, 255);

        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + barW, y + barH), bgCol, 4.f);
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + (barW * progress), y + barH), progressCol, 4.f);

        char buf[32];
        sprintf_s(buf, "BOMB: %.2fs", timer);
        ImVec2 textSz = ImGui::GetFont()->CalcTextSizeA(14.f, FLT_MAX, 0.f, buf);
        dl->AddText(ImVec2(x + (barW - textSz.x) * 0.5f, y + (barH - textSz.y) * 0.5f), IM_COL32(255, 255, 255, 255), buf);
        break; // Only draw one bomb timer
    }
}

void Visuals::RenderSpectatorList() {
    uintptr_t lcAddr = Offsets::Get("dwLocalPlayerController");
    uintptr_t listAddr = Offsets::Get("dwEntityList");
    if (!lcAddr || !listAddr) return;

    uintptr_t localCtrl = CS2::Read<uintptr_t>(lcAddr);
    uintptr_t entityList = CS2::Read<uintptr_t>(listAddr);
    if (!localCtrl || !entityList) return;

    uintptr_t localPawn = CS2::GetPawn(entityList, localCtrl);
    if (!localPawn) return;

    uint32_t myPawnHandle = CS2::Read<uint32_t>(localCtrl + 0x83C); // m_hPlayerPawn

    std::vector<std::string> spectators;

    // Enumerate player controllers
    for (int i = 1; i <= 64; ++i) {
        uintptr_t ctrl = CS2::GetEntityByIndex(entityList, i);
        if (!ctrl || ctrl == localCtrl) continue;

        uintptr_t pawn = CS2::GetPawn(entityList, ctrl);
        if (!pawn) continue;

        // CS2 observer target handle is typically at pawn + m_pObserverServices (0x1518) -> m_hObserverTarget (0x44)
        uintptr_t observerServices = CS2::Read<uintptr_t>(pawn + 0x1518);
        if (observerServices) {
            uint32_t targetHandle = CS2::Read<uint32_t>(observerServices + 0x44);
            if (targetHandle == myPawnHandle) {
                std::string name = CS2::GetName(ctrl);
                if (!name.empty()) spectators.push_back(name);
            }
        }
    }

    if (spectators.empty()) return;

    // Draw Spectator List Window/Panel
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    float x = ImGui::GetIO().DisplaySize.x - 220.f;
    float y = 150.f;

    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + 200.f, y + 25.f + (spectators.size() * 18.f)), IM_COL32(18, 18, 22, 230), 4.f);
    dl->AddRect(ImVec2(x, y), ImVec2(x + 200.f, y + 25.f + (spectators.size() * 18.f)), IM_COL32(50, 50, 60, 255), 4.f);
    dl->AddText(ImVec2(x + 10.f, y + 5.f), IM_COL32(0, 200, 255, 255), "SPECTATORS");

    for (size_t i = 0; i < spectators.size(); ++i) {
        dl->AddText(ImVec2(x + 10.f, y + 25.f + (i * 18.f)), IM_COL32(220, 220, 220, 255), spectators[i].c_str());
    }
}

void Visuals::RenderRadar() {
    uintptr_t lcAddr = Offsets::Get("dwLocalPlayerController");
    uintptr_t lpAddr = Offsets::Get("dwLocalPlayerPawn");
    uintptr_t listAddr = Offsets::Get("dwEntityList");
    uintptr_t viewAngAddr = Offsets::Get("dwViewAngles");
    if (!lcAddr || !lpAddr || !listAddr || !viewAngAddr) return;

    uintptr_t localCtrl = CS2::Read<uintptr_t>(lcAddr);
    uintptr_t localPawn = CS2::Read<uintptr_t>(lpAddr);
    uintptr_t entityList = CS2::Read<uintptr_t>(listAddr);
    if (!localCtrl || !localPawn || !entityList) return;

    Vector3 localOrigin = CS2::GetAbsOrigin(localPawn);
    Vector3 viewAng = CS2::Read<Vector3>(viewAngAddr);

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    float rx = 20.f;
    float ry = 100.f;
    float rSize = 150.f;
    float rCenter = rSize * 0.5f;

    // Draw Radar Outline / Grid
    dl->AddRectFilled(ImVec2(rx, ry), ImVec2(rx + rSize, ry + rSize), IM_COL32(18, 18, 22, 230), 4.f);
    dl->AddRect(ImVec2(rx, ry), ImVec2(rx + rSize, ry + rSize), IM_COL32(50, 50, 60, 255), 4.f);
    dl->AddLine(ImVec2(rx + rCenter, ry), ImVec2(rx + rCenter, ry + rSize), IM_COL32(50, 50, 60, 150));
    dl->AddLine(ImVec2(rx, ry + rCenter), ImVec2(rx + rSize, ry + rCenter), IM_COL32(50, 50, 60, 150));

    // Draw local player indicator (centered, pointing forward)
    dl->AddCircleFilled(ImVec2(rx + rCenter, ry + rCenter), 3.f, IM_COL32(0, 200, 255, 255));

    float angleRad = (viewAng.y) * 3.14159265f / 180.f;
    float dx = cosf(angleRad) * 10.f;
    float dy = sinf(angleRad) * 10.f;
    dl->AddLine(ImVec2(rx + rCenter, ry + rCenter), ImVec2(rx + rCenter + dx, ry + rCenter - dy), IM_COL32(0, 200, 255, 255), 1.5f);

    int localTeam = CS2::GetTeam(localPawn);

    // Enumerate players and map relative coordinates
    for (int i = 1; i <= 64; ++i) {
        uintptr_t ctrl = CS2::GetEntityByIndex(entityList, i);
        if (!ctrl || ctrl == localCtrl) continue;

        uintptr_t pawn = CS2::GetPawn(entityList, ctrl);
        if (!pawn) continue;

        int hp = CS2::GetHealth(pawn);
        if (hp <= 0 || hp > 100) continue;

        int team = CS2::GetTeam(pawn);
        bool isEnemy = (team != localTeam);

        Vector3 pos = CS2::GetAbsOrigin(pawn);
        if (pos.x == 0.f && pos.y == 0.f) continue;

        // Delta position
        float diffX = pos.x - localOrigin.x;
        float diffY = pos.y - localOrigin.y;

        // Rotate relative to player view angle
        float rotX = diffY * cosf(angleRad) - diffX * sinf(angleRad);
        float rotY = diffX * cosf(angleRad) + diffY * sinf(angleRad);

        // Scale factors: 0.05f corresponds to ~2000 units max radar distance
        float scale = 0.05f;
        float dotX = rCenter + rotX * scale;
        float dotY = rCenter + rotY * scale;

        // Clamp to radar boundaries
        if (dotX < 2.f) dotX = 2.f;
        if (dotX > rSize - 2.f) dotX = rSize - 2.f;
        if (dotY < 2.f) dotY = 2.f;
        if (dotY > rSize - 2.f) dotY = rSize - 2.f;

        ImU32 dotCol = isEnemy ? IM_COL32(255, 50, 50, 255) : IM_COL32(80, 160, 255, 255);
        dl->AddCircleFilled(ImVec2(rx + dotX, ry + dotY), 2.5f, dotCol);
    }
}

void Visuals::AddHitMarker(bool headshot) {
    HitMarker m; m.time = GetTickCount(); m.headshot = headshot;
    m_hitMarkers.push_back(m);
}

void Visuals::RenderIndicators() {
    Config* cfg = g_Cheat ? g_Cheat->GetConfig() : nullptr;
    if (!cfg) return;

    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    // Align indicators in a clean, vertical stack on the left side of the screen
    float startX = 20.f;
    float startY = 270.f;
    float width = 160.f;
    float height = 30.f;
    float spacing = 8.f;

    struct IndicatorItem {
        const char* name;
        bool active;
        const char* modeText;
        ImU32 activeColor;
    };

    std::vector<IndicatorItem> items;

    // 1. Aimbot Status
    if (cfg->m_ragebotEnabled) {
        items.push_back({ "RAGE AIM", true, "ACTIVE", IM_COL32(255, 50, 50, 255) });
    } else if (cfg->m_aimbotEnabled) {
        items.push_back({ "LEGIT AIM", true, "ACTIVE", IM_COL32(0, 200, 255, 255) });
    } else {
        items.push_back({ "AIMBOT", false, "DISABLED", IM_COL32(100, 100, 100, 255) });
    }

    // 2. Silent Aim / pSilent Status
    if (cfg->m_ragebotEnabled) {
        bool silent = cfg->m_ragebotSilentAimbot;
        items.push_back({ "pSILENT", silent, silent ? "ON" : "OFF", silent ? IM_COL32(0, 255, 120, 255) : IM_COL32(180, 180, 180, 255) });
    }

    // 3. Anti-Aim (Desync / FakeLag)
    if (cfg->m_antiaimEnabled) {
        items.push_back({ "DESYNC", cfg->m_antiaimDesync, cfg->m_antiaimDesync ? "ACTIVE" : "INERT", cfg->m_antiaimDesync ? IM_COL32(0, 200, 255, 255) : IM_COL32(180, 180, 180, 255) });
        if (cfg->m_antiaimFakeLag) {
            items.push_back({ "FAKELAG", true, "ACTIVE", IM_COL32(255, 180, 0, 255) });
        }
    }

    // 4. Knifebot Status
    if (cfg->m_knifeBot) {
        items.push_back({ "KNIFEBOT", true, "READY", IM_COL32(200, 50, 255, 255) });
    }

    // Draw indicators stack
    for (size_t i = 0; i < items.size(); ++i) {
        float y = startY + i * (height + spacing);
        
        // Deep glassmorphic panel background
        dl->AddRectFilled(ImVec2(startX, y), ImVec2(startX + width, y + height), IM_COL32(10, 12, 16, 200), 5.f);
        
        // Premium accent left-border glow line
        dl->AddLine(ImVec2(startX, y + 2.f), ImVec2(startX, y + height - 2.f), items[i].active ? items[i].activeColor : IM_COL32(80, 80, 90, 255), 2.5f);

        // Indicator Name Text
        dl->AddText(ImVec2(startX + 10.f, y + height * 0.5f - 6.f), IM_COL32(240, 240, 245, 255), items[i].name);

        // State indicator text (right-aligned)
        ImVec2 size = ImGui::CalcTextSize(items[i].modeText);
        dl->AddText(ImVec2(startX + width - size.x - 10.f, y + height * 0.5f - 6.f), items[i].active ? items[i].activeColor : IM_COL32(120, 120, 130, 255), items[i].modeText);
    }
}

