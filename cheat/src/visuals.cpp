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
#include "game_classes.h"
#include "cheat_core.h"
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
    {0,1},{1,2},{2,3},{3,4},{4,5},
    // Left arm
    {3,6},{6,7},{7,8},{8,9},
    // Right arm
    {3,10},{10,11},{11,12},{12,13},
    // Left leg
    {0,14},{14,15},{15,16},
    // Right leg
    {0,18},{18,19},{19,20},
};
static const int kNumBoneLinks = (int)(sizeof(kBoneLinks)/sizeof(kBoneLinks[0]));
// Highest bone index we use: must read at least 21 bones
static const int kMaxBone = 21;

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
    if (!cfg->m_espEnabled) return;

    uintptr_t lcAddr   = Offsets::Get("dwLocalPlayerController");
    uintptr_t lpAddr   = Offsets::Get("dwLocalPlayerPawn");
    uintptr_t listAddr = Offsets::Get("dwEntityList");
    if (!lcAddr || !lpAddr || !listAddr) return;

    uintptr_t localCtrl = CS2::Read<uintptr_t>(lcAddr);
    uintptr_t localPawn = CS2::Read<uintptr_t>(lpAddr);
    uintptr_t entityList = CS2::Read<uintptr_t>(listAddr);
    if (!localCtrl || !localPawn || !entityList) return;

    int localTeam = CS2::GetTeam(localCtrl);
    Vector3 localOrigin = CS2::GetAbsOrigin(localPawn);

    Matrix4x4 vm = CS2::GetViewMatrix();

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

        int team = CS2::GetTeam(ctrl);
        if (team != 2 && team != 3) continue;
        bool isEnemy = (team != localTeam);
        if (!isEnemy && !cfg->m_espTeammates) continue;

        Vector3 origin = CS2::GetAbsOrigin(pawn);
        if (origin.x == 0.f && origin.y == 0.f && origin.z == 0.f) continue;

        // ---- Bone positions ----
        PlayerInfo pi{};
        pi.pawn    = pawn;
        pi.ctrl    = ctrl;
        pi.hp      = hp;
        pi.team    = team;
        pi.isEnemy = isEnemy;
        pi.origin  = origin;
        pi.armor   = CS2::Read<int>(pawn + 0xEB0); // m_ArmorValue approx
        pi.scoped  = CS2::Read<bool>(pawn + 0x1428); // m_bIsScoped fallback
        pi.bonesValid = false;

        uintptr_t boneArr = CS2::GetBoneArray(pawn);
        Vector3 headPos = { origin.x, origin.y, origin.z + 72.f }; // fallback
        if (boneArr) {
            pi.bonesValid = true;
            for (int b = 0; b <= kMaxBone; ++b)
                pi.bones[b] = CS2::GetBonePos(boneArr, b);
            // Bone 5 = head in standard CS2 model
            if (pi.bones[5].x != 0.f || pi.bones[5].y != 0.f)
                headPos = pi.bones[5];
        }
        pi.head = headPos;

        // ---- World to screen ----
        Vector3 topPos = { headPos.x, headPos.y, headPos.z + 8.f };
        Vector2 scrFeet, scrHead, scrTop;
        if (!Utils::WorldToScreen(origin,  scrFeet, vm)) continue;
        if (!Utils::WorldToScreen(headPos, scrHead, vm)) continue;
        Utils::WorldToScreen(topPos, scrTop, vm);

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

        // ---- Box ----
        if (cfg->m_espBox) {
            // Corner box style (like neverlose.cc)
            float cw = w * 0.25f;
            float ch = h * 0.2f;
            ImU32 outline = IM_COL32(0,0,0,180);
            // Outer/inner shadow outlines
            dl->AddRect(ImVec2(x0-1.f,y0-1.f), ImVec2(x1+1.f,y1+1.f), outline, 0.f, 1.f);
            dl->AddRect(ImVec2(x0+1.f,y0+1.f), ImVec2(x1-1.f,y1-1.f), outline, 0.f, 1.f);
            // Corner lines
            auto corners = [&](float bx, float by, float ddx, float ddy){
                dl->AddLine(ImVec2(bx,by), ImVec2(bx+ddx*cw,by),    col32, 1.5f);
                dl->AddLine(ImVec2(bx,by), ImVec2(bx,by+ddy*ch),    col32, 1.5f);
            };
            corners(x0,y0, 1.f, 1.f); corners(x1,y0,-1.f, 1.f);
            corners(x0,y1, 1.f,-1.f); corners(x1,y1,-1.f,-1.f);
        }

        // ---- Health bar (left side) ----
        if (cfg->m_espHealthBar) {
            float pct   = hp / 100.f;
            float barH  = h * pct;
            float bx    = x0 - 5.f;
            ImU32 hcol  = hp > 60 ? IM_COL32(0,220,0,255) :
                          hp > 30 ? IM_COL32(220,200,0,255) :
                                    IM_COL32(220,50,50,255);
            dl->AddRectFilled(ImVec2(bx-2,y0),        ImVec2(bx,y1),         IM_COL32(0,0,0,160));
            dl->AddRectFilled(ImVec2(bx-2,y1-barH),   ImVec2(bx,y1),         hcol);
        }

        // ---- Armor bar (right side) ----
        if (cfg->m_espArmorBar && pi.armor > 0) {
            float pct   = pi.armor / 100.f;
            float barH  = h * pct;
            float bx    = x1 + 3.f;
            dl->AddRectFilled(ImVec2(bx,y0),        ImVec2(bx+2,y1),       IM_COL32(0,0,0,160));
            dl->AddRectFilled(ImVec2(bx,y1-barH),   ImVec2(bx+2,y1),       IM_COL32(80,160,255,255));
        }

        // ---- Name ----
        if (cfg->m_espName) {
            ImVec2 ts = ImGui::CalcTextSize(pi.name.c_str());
            dl->AddText(ImVec2(scrTop.x - ts.x*0.5f, y0 - ts.y - 2), col32, pi.name.c_str());
        }

        // ---- Weapon ----
        if (cfg->m_espWeapon && !pi.weapon.empty()) {
            ImVec2 ts = ImGui::CalcTextSize(pi.weapon.c_str());
            dl->AddText(ImVec2(scrTop.x - ts.x*0.5f, y1 + 2), IM_COL32(220,220,220,255), pi.weapon.c_str());
        }

        // ---- Distance ----
        if (cfg->m_espDistance) {
            float dx=origin.x-localOrigin.x, dy=origin.y-localOrigin.y, dz=origin.z-localOrigin.z;
            int dist = (int)(sqrtf(dx*dx+dy*dy+dz*dz) * 0.0254f); // hammer→meters
            char buf[16]; sprintf_s(buf, "%dm", dist);
            ImVec2 ts = ImGui::CalcTextSize(buf);
            dl->AddText(ImVec2(scrTop.x - ts.x*0.5f, y1 + (cfg->m_espWeapon && !pi.weapon.empty() ? 14.f : 2.f)),
                        IM_COL32(200,200,200,200), buf);
        }

        // ---- Flags ----
        if (cfg->m_espFlags) {
            float fy = y0;
            auto addFlag = [&](const char* txt, ImU32 c){
                dl->AddText(ImVec2(x1+4.f, fy), c, txt);
                fy += 13.f;
            };
            if (pi.scoped) addFlag("SCOPED", IM_COL32(0,220,255,255));
            uint32_t flags = CS2::Read<uint32_t>(pawn + 0x3F8);
            if (!(flags&1)) addFlag("AIR", IM_COL32(255,220,80,255));
            bool reloading = CS2::Read<bool>(pawn + 0x1468); // rough offset
            if (reloading) addFlag("RELOAD", IM_COL32(255,140,0,255));
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
                if (!Utils::WorldToScreen(wp, sp, vm)) continue;
                if (!Utils::WorldToScreen(wc, sc, vm)) continue;
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
    if (cfg->m_hitMarker) RenderHitMarkers();

    // ---- Bomb timer ----
    if (cfg->m_bombTimer) RenderBombTimer();

    // ---- Spectator list ----
    if (cfg->m_spectatorList) RenderSpectatorList();

    // ---- Radar ----
    if (cfg->m_radar) RenderRadar();
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

    int localTeam = CS2::GetTeam(localCtrl);

    // Enumerate players and map relative coordinates
    for (int i = 1; i <= 64; ++i) {
        uintptr_t ctrl = CS2::GetEntityByIndex(entityList, i);
        if (!ctrl || ctrl == localCtrl) continue;

        uintptr_t pawn = CS2::GetPawn(entityList, ctrl);
        if (!pawn) continue;

        int hp = CS2::GetHealth(pawn);
        if (hp <= 0 || hp > 100) continue;

        int team = CS2::GetTeam(ctrl);
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

