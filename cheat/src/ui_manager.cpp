// =================================================================
// ui_manager.cpp - UI Manager implementation
// =================================================================

#include "ui_manager.h"
#include "cheat_core.h"
#include <vector>
#include "lua_api.h"
#include "logger.h"
#include "config.h"
#include "game_classes.h"
#include "offsets.h"
#include "memory.h"
#include "visuals.h"
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

// Global UI manager instance
UIManager* g_UIManager = nullptr;

// -----------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------
UIManager::UIManager()
    : m_initialized(false)
    , m_rendererReady(false)
    , m_menuOpen(true)
{
}

// -----------------------------------------------------------------
// Destructor
// -----------------------------------------------------------------
UIManager::~UIManager() {
    Shutdown();
}

// -----------------------------------------------------------------
// Initialize UI
// -----------------------------------------------------------------
bool UIManager::Initialize() {
    if (m_initialized) {
        return true;
    }

    Logger::Log("Initializing UI...");

    // Setup ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.MouseDrawCursor = false;                          // don't draw software cursor over game
    io.ConfigFlags    |= ImGuiConfigFlags_NoMouseCursorChange; // don't override system cursor

    // Setup style
    SetupStyle();

    // Load fonts
    LoadFonts();

    // Setup platform/renderer backends
    // (Implementation requires DX11 or other renderer)

    m_initialized = true;
    Logger::Log("UI initialized successfully");
    return true;
}

// -----------------------------------------------------------------
// Shutdown UI
// -----------------------------------------------------------------
void UIManager::Shutdown() {
    if (!m_initialized) {
        return;
    }

    Logger::Log("Shutting down UI...");

    ImGui::DestroyContext();

    m_initialized = false;
    Logger::Log("UI shutdown complete");
}

// -----------------------------------------------------------------
// Update UI
// -----------------------------------------------------------------
bool UIManager::InitRenderer(ID3D11Device* device, ID3D11DeviceContext* ctx, HWND hwnd) {
    if (m_rendererReady) return true;

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(device, ctx);

    m_rendererReady = true;
    Logger::Log("DX11 renderer backend initialized");
    return true;
}

void UIManager::Update() {
    // Do nothing until the DX11 hook has set up the renderer backend
    if (!m_initialized || !m_rendererReady) return;

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // Hide ImGui cursor when menu is closed so it doesn't overlay the game
    ImGui::SetMouseCursor(m_menuOpen ? ImGuiMouseCursor_Arrow : ImGuiMouseCursor_None);

    HandleInput();
}

// -----------------------------------------------------------------
// Render UI
// -----------------------------------------------------------------
// ---------------------------------------------------------------------------
// ESP — drawn every frame on the background draw list (behind the menu)
// ---------------------------------------------------------------------------
void UIManager::RenderESP() {
    if (!g_Cheat) return;
    Config* cfg = g_Cheat->GetConfig();
    if (!cfg || !cfg->m_visualsEnabled || !cfg->m_espEnabled) return;

    uintptr_t clientBase    = Memory::GetClientBase();
    uintptr_t listAddr      = Offsets::Get("dwEntityList");
    uintptr_t localCtrlAddr = Offsets::Get("dwLocalPlayerController");
    if (!clientBase || !listAddr || !localCtrlAddr) return;

    uintptr_t entityList = CS2::Read<uintptr_t>(listAddr);
    uintptr_t localCtrl  = CS2::Read<uintptr_t>(localCtrlAddr);
    if (!entityList || !localCtrl) return;

    int       localTeam = CS2::GetTeam(localCtrl);
    Matrix4x4 vm        = CS2::GetViewMatrix();

    ImDrawList* dl  = ImGui::GetBackgroundDrawList();
    ImVec2      disp = ImGui::GetIO().DisplaySize;
    int sw = (int)disp.x, sh = (int)disp.y;
    if (sw <= 0 || sh <= 0) return;

    int nCtrl = 0, nPawn = 0, nAlive = 0, nDrawn = 0;
    (void)nCtrl; (void)nPawn; (void)nAlive; (void)nDrawn;

    for (int i = 1; i <= 1024; ++i) {
        uintptr_t ctrl = CS2::GetEntityByIndex(entityList, i);
        if (!ctrl || ctrl == localCtrl) continue;
        nCtrl++;

        // Only draw actual players (team 2=T, 3=CT)
        int team = CS2::GetTeam(ctrl);
        if (team != 2 && team != 3) continue;
        // In deathmatch all teams are enemies; only skip true teammates if option is off
        bool isTeammate = (team == localTeam);
        if (isTeammate && !cfg->m_espTeammates) continue;

        uintptr_t pawn = CS2::GetPawn(entityList, ctrl);
        if (!pawn) continue;
        nPawn++;

        int hp = CS2::GetHealth(pawn);
        if (hp <= 0 || hp > 100) continue;
        nAlive++;

        Vector3 origin = CS2::GetAbsOrigin(pawn);
        if (origin.x == 0.f && origin.y == 0.f && origin.z == 0.f) continue;
        // CS2: m_vecAbsOrigin is at player feet; standing height ~72u, head ~68u
        Vector3 head = { origin.x, origin.y, origin.z + 68.f };

        Vector2 sOrigin, sHead;
        if (!Utils::WorldToScreen(origin, sOrigin, vm, sw, sh)) continue;
        if (!Utils::WorldToScreen(head,   sHead,   vm, sw, sh)) continue;

        float boxH = sOrigin.y - sHead.y;
        if (boxH < 2.f || boxH > (float)sh) continue;
        float boxW = boxH * 0.45f;
        nDrawn++;

        ImU32 col = isTeammate
                    ? IM_COL32(0, 200, 100, 230)   // green = teammate
                    : IM_COL32(255, 50,  50,  230); // red   = enemy

        float x1 = sHead.x - boxW * 0.5f, y1 = sHead.y;
        float x2 = sHead.x + boxW * 0.5f, y2 = sOrigin.y;

        if (cfg->m_espBox)
            dl->AddRect(ImVec2(x1, y1), ImVec2(x2, y2), col, 0.f, 1.5f);

        if (cfg->m_espHealthBar) {
            float bx = x1 - 5.f, bh = y2 - y1, fh = bh * (hp / 100.f);
            ImU32 hc = IM_COL32((int)(255*(1.f-hp/100.f)),(int)(255*(hp/100.f)),0,230);
            dl->AddRectFilled(ImVec2(bx-2,y1),ImVec2(bx,y2),IM_COL32(0,0,0,160));
            dl->AddRectFilled(ImVec2(bx-2,y2-fh),ImVec2(bx,y2),hc);
            char t[8]; sprintf_s(t,"%d",hp);
            dl->AddText(ImVec2(bx-18.f,y2-fh-6.f),hc,t);
        }

        if (cfg->m_espName) {
            std::string name = CS2::GetName(ctrl);
            if (!name.empty())
                dl->AddText(ImVec2(x1, y1-14.f), IM_COL32(255,255,255,230), name.c_str());
        }

        // Skeleton ESP — dynamic: find all valid bones, auto-connect nearby ones
        if (cfg->m_espSkeleton) {
            uintptr_t boneArr = CS2::GetBoneArray(pawn);
            if (boneArr) {
                // Collect valid bone positions (within player bounding box)
                struct BonePt { Vector3 w; Vector2 s; bool onScreen; };
                std::vector<BonePt> pts;

                for (int b = 0; b < 128; b++) {
                    Vector3 bp = CS2::GetBonePos(boneArr, b);
                    // Filter: must be within player extent (~60 radius, -10 to 90 vertical)
                    float dx = bp.x - origin.x, dy = bp.y - origin.y, dz = bp.z - origin.z;
                    if (bp.x==0&&bp.y==0&&bp.z==0) continue;
                    if (fabsf(dx)>60||fabsf(dy)>60||dz<-20||dz>100) continue;
                    Vector2 sp;
                    bool vis = Utils::WorldToScreen(bp, sp, vm, sw, sh);
                    pts.push_back({bp, sp, vis});
                }

                // Connect bones that are within 35 units of each other in 3D
                // This creates a natural skeleton without needing exact indices
                for (size_t i = 0; i < pts.size(); i++) {
                    for (size_t j = i+1; j < pts.size(); j++) {
                        if (!pts[i].onScreen || !pts[j].onScreen) continue;
                        float dx = pts[i].w.x-pts[j].w.x;
                        float dy = pts[i].w.y-pts[j].w.y;
                        float dz = pts[i].w.z-pts[j].w.z;
                        float d = sqrtf(dx*dx+dy*dy+dz*dz);
                        if (d > 0.5f && d < 32.f) {
                            dl->AddLine(ImVec2(pts[i].s.x,pts[i].s.y),
                                        ImVec2(pts[j].s.x,pts[j].s.y),
                                        col, 1.2f);
                        }
                    }
                }
            }
        }
    }

    // Spread circle around crosshair (base inaccuracy indicator)
    if (cfg->m_espEnabled) {
        float cx = sw * 0.5f, cy = sh * 0.5f;
        dl->AddCircle(ImVec2(cx,cy), 2.5f, IM_COL32(255,255,255,200), 12, 1.f); // dot center
    }
}

void UIManager::Render() {
    if (!m_initialized || !m_rendererReady) return;

    RenderESP();

    if (m_menuOpen) RenderMenu();

    ImGui::Render();
    ImDrawData* dd = ImGui::GetDrawData();
    if (dd) ImGui_ImplDX11_RenderDrawData(dd);
}

// -----------------------------------------------------------------
// Setup ImGui style
// -----------------------------------------------------------------
void UIManager::SetupStyle() {
    ImGuiStyle& style = ImGui::GetStyle();

    // Colors
    style.Colors[ImGuiCol_WindowBg] = ImColor(18, 18, 22, 240);
    style.Colors[ImGuiCol_TitleBg] = ImColor(35, 35, 42, 255);
    style.Colors[ImGuiCol_TitleBgActive] = ImColor(55, 55, 65, 255);
    style.Colors[ImGuiCol_Button] = ImColor(40, 40, 50, 255);
    style.Colors[ImGuiCol_ButtonHovered] = ImColor(60, 60, 75, 255);
    style.Colors[ImGuiCol_ButtonActive] = ImColor(80, 80, 100, 255);
    style.Colors[ImGuiCol_CheckMark] = ImColor(0, 200, 255, 255);
    style.Colors[ImGuiCol_SliderGrab] = ImColor(0, 200, 255, 255);
    style.Colors[ImGuiCol_SliderGrabActive] = ImColor(50, 220, 255, 255);
    style.Colors[ImGuiCol_FrameBg] = ImColor(30, 30, 38, 255);
    style.Colors[ImGuiCol_FrameBgHovered] = ImColor(40, 40, 50, 255);
    style.Colors[ImGuiCol_FrameBgActive] = ImColor(50, 50, 60, 255);
    style.Colors[ImGuiCol_Tab] = ImColor(30, 30, 38, 255);
    style.Colors[ImGuiCol_TabHovered] = ImColor(50, 50, 60, 255);
    style.Colors[ImGuiCol_TabSelected] = ImColor(0, 200, 255, 255);
    style.Colors[ImGuiCol_Header] = ImColor(0, 200, 255, 50);
    style.Colors[ImGuiCol_HeaderHovered] = ImColor(0, 200, 255, 100);
    style.Colors[ImGuiCol_HeaderActive] = ImColor(0, 200, 255, 150);
    style.Colors[ImGuiCol_Separator] = ImColor(50, 50, 60, 255);
    style.Colors[ImGuiCol_SeparatorHovered] = ImColor(0, 200, 255, 100);
    style.Colors[ImGuiCol_SeparatorActive] = ImColor(0, 200, 255, 200);

    // Rounding
    style.WindowRounding = 8.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.ChildRounding = 6.0f;

    // Sizes
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;
}

// -----------------------------------------------------------------
// Load fonts
// -----------------------------------------------------------------
void UIManager::LoadFonts() {
    ImGuiIO& io = ImGui::GetIO();

    // Default font
    io.Fonts->AddFontDefault();

    // Try to load custom fonts
    // (Implementation requires font files)
}

// -----------------------------------------------------------------
// Handle input
// -----------------------------------------------------------------
void UIManager::HandleInput() {
    // Toggle menu with INSERT key
    if (GetAsyncKeyState(VK_INSERT) & 1) {
        m_menuOpen = !m_menuOpen;
    }
}

// -----------------------------------------------------------------
// Render menu
// -----------------------------------------------------------------
void UIManager::RenderMenu() {
    ImGui::SetNextWindowSize(ImVec2(750, 500), ImGuiCond_Once);
    ImGui::SetNextWindowPos(ImVec2(100, 100), ImGuiCond_Once);
    ImGui::Begin("Neverlose.cc v1.0", &m_menuOpen, ImGuiWindowFlags_NoCollapse);

    // Tabs
    if (ImGui::BeginTabBar("MainTabs")) {
        if (ImGui::BeginTabItem("Ragebot")) {
            RenderRagebotTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Legit")) {
            RenderLegitbotTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Anti-Aim")) {
            RenderAntiAimTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Visuals")) {
            RenderVisualsTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Misc")) {
            RenderMiscTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Config")) {
            RenderConfigTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Lua")) {
            RenderLuaTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

// -----------------------------------------------------------------
// Render ragebot tab
// -----------------------------------------------------------------
void UIManager::RenderRagebotTab() {
    Config* config = g_Cheat->GetConfig();

    ImGui::TextColored(ImVec4(0,1,0.4f,1), "Ragebot (active) — aims via dwViewAngles, fires via dwForceAttack");
    ImGui::TextColored(ImVec4(0.6f,0.6f,0.6f,1), "Auto Fire off = hold LMB to assist. Smooth>1 slows the snap.");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::BeginChild("RagebotLeft", ImVec2(ImGui::GetContentRegionAvail().x * 0.5f, 0), true);
    ImGui::Checkbox("Ragebot Enabled", &config->m_ragebotEnabled);
    ImGui::SliderFloat("FOV", &config->m_ragebotFOV, 0.0f, 180.0f, "%.0f deg");
    ImGui::SliderFloat("Smooth", &config->m_ragebotSmooth, 0.0f, 10.0f, "%.1f (>=1 instant)");
    ImGui::SliderFloat("Hitchance", &config->m_ragebotHitchance, 0.0f, 100.0f, "%.0f%%");
    ImGui::SliderFloat("Min Damage", &config->m_ragebotMinDamage, 0.0f, 100.0f, "%.0f hp");
    ImGui::Checkbox("Auto Fire", &config->m_ragebotAutoFire);
    ImGui::Checkbox("Auto Stop", &config->m_ragebotAutoStop);
    ImGui::Checkbox("Extrapolation", &config->m_ragebotExtrapolation);
    ImGui::Checkbox("Backtrack", &config->m_ragebotBacktrack);
    ImGui::SliderFloat("Backtrack Time", &config->m_ragebotBacktrackTime, 0.0f, 0.5f, "%.2f s");
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("RagebotRight", ImVec2(0, 0), true);
    ImGui::Checkbox("Quick Scope", &config->m_ragebotQuickScope);
    ImGui::Checkbox("Visual Aimbot", &config->m_ragebotVisualAimbot);
    ImGui::Checkbox("Leg Movement", &config->m_ragebotLegMovement);
    ImGui::Checkbox("Multipoint", &config->m_ragebotMultipoint);
    ImGui::SliderFloat("Multipoint Scale", &config->m_ragebotMultipointScale, 0.0f, 1.0f);
    ImGui::Checkbox("Resolver", &config->m_ragebotResolver);
    ImGui::Combo("Resolver Mode", &config->m_ragebotResolverMode, "Auto\0LBY\0History\0");
    ImGui::EndChild();
}

// -----------------------------------------------------------------
// Legitbot tab — triggerbot + smooth aim
// -----------------------------------------------------------------
void UIManager::RenderLegitbotTab() {
    Config* config = g_Cheat->GetConfig();

    ImGui::BeginChild("LegitLeft", ImVec2(ImGui::GetContentRegionAvail().x * 0.5f, 0), true);
    ImGui::Text("Triggerbot");
    ImGui::Separator();
    ImGui::Checkbox("Enabled##trig",  &config->m_triggerbotEnabled);
    ImGui::SliderFloat("FOV##trig",   &config->m_triggerbotFov,   0.5f, 10.f, "%.1f deg");
    ImGui::SliderInt("Delay (ms)",    &config->m_triggerbotDelay, 0,    200);
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.5f,1,0.5f,1), "Auto-fires when crosshair on enemy");
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("LegitRight", ImVec2(0, 0), true);
    ImGui::Text("Legit Aimbot");
    ImGui::Separator();
    ImGui::Checkbox("Enabled##la",   &config->m_aimbotEnabled);
    ImGui::SliderFloat("FOV##la",    &config->m_aimbotFov,    0.5f, 20.f, "%.1f deg");
    ImGui::SliderFloat("Smooth##la", &config->m_aimbotSmooth, 2.f,  20.f, "%.1f");
    ImGui::Checkbox("No teamkill##la", &config->m_aimbotTeamcheck);
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(1,1,0,1), "Hold LMB to activate");
    ImGui::EndChild();
}

// -----------------------------------------------------------------
// Render anti-aim tab
// -----------------------------------------------------------------
void UIManager::RenderAntiAimTab() {
    Config* config = g_Cheat->GetConfig();

    ImGui::TextColored(ImVec4(0,1,0.4f,1), "Anti-Aim (active) — writes dwViewAngles each tick");
    ImGui::Separator();

    ImGui::BeginChild("AntiAimLeft", ImVec2(ImGui::GetContentRegionAvail().x * 0.5f, 0), true);
    ImGui::Checkbox("Anti-Aim Enabled", &config->m_antiaimEnabled);
    ImGui::Combo("Yaw Mode", &config->m_antiaimMode,
        "Backward\0Jitter\0Spin\0Sideways\0Desync\0Jitter 3-Way\0Custom\0");
    ImGui::SliderFloat("Spin Speed", &config->m_antiaimSpinSpeed, 1.0f, 20.0f, "%.1f deg/tick");
    ImGui::Separator();
    ImGui::Combo("Pitch Mode", &config->m_antiaimPitchMode,
        "Down\0Up\0Zero\0Custom\0");
    ImGui::SliderFloat("Pitch (custom)", &config->m_antiaimPitch, -89.0f, 89.0f, "%.0f deg");
    ImGui::Separator();
    ImGui::Checkbox("Desync", &config->m_antiaimDesync);
    ImGui::SliderFloat("Desync Amount", &config->m_antiaimDesyncAmount, 0.0f, 180.0f, "%.0f deg");
    ImGui::Checkbox("Invert on Shot", &config->m_antiaimInvertOnShot);
    ImGui::Checkbox("Fake Lag", &config->m_antiaimFakeLag);
    ImGui::SliderFloat("Fake Lag Amount", &config->m_antiaimFakeLagAmount, 0.0f, 20.0f);
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("AntiAimRight", ImVec2(0, 0), true);
    ImGui::Checkbox("Choke Packets", &config->m_antiaimChokePackets);
    ImGui::SliderInt("Choke Percentage", &config->m_antiaimChokePercent, 0, 100);
    ImGui::Checkbox("LBY Manipulation", &config->m_antiaimLBY);
    ImGui::SliderFloat("LBY Offset", &config->m_antiaimLBYOffset, -180.0f, 180.0f);
    ImGui::Checkbox("Fake Angle", &config->m_antiaimFakeAngle);
    ImGui::SliderFloat("Fake Angle Offset", &config->m_antiaimFakeAngleOffset, -180.0f, 180.0f);
    ImGui::Checkbox("On Air", &config->m_antiaimOnAir);
    ImGui::Checkbox("On Ground", &config->m_antiaimOnGround);
    ImGui::Checkbox("Edge", &config->m_antiaimEdge);
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(1,0.8f,0,1), "Note: true desync / packet choke need");
    ImGui::TextColored(ImVec4(1,0.8f,0,1), "the user cmd; visible yaw is applied now.");
    ImGui::EndChild();
}

// -----------------------------------------------------------------
// Render visuals tab
// -----------------------------------------------------------------
void UIManager::RenderVisualsTab() {
    Config* config = g_Cheat->GetConfig();

    ImGui::BeginChild("VisualsLeft", ImVec2(ImGui::GetContentRegionAvail().x * 0.333f, 0), true);
    ImGui::Checkbox("Visuals Enabled", &config->m_visualsEnabled);
    ImGui::Separator();
    ImGui::Checkbox("ESP Enabled", &config->m_espEnabled);
    ImGui::Checkbox("Box", &config->m_espBox);
    ImGui::Checkbox("Health Bar", &config->m_espHealthBar);
    ImGui::Checkbox("Armor Bar", &config->m_espArmorBar);
    ImGui::Checkbox("Name", &config->m_espName);
    ImGui::Checkbox("Weapon", &config->m_espWeapon);
    ImGui::Checkbox("Flags", &config->m_espFlags);
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("VisualsCenter", ImVec2(ImGui::GetContentRegionAvail().x * 0.333f, 0), true);
    ImGui::Checkbox("Skeleton", &config->m_espSkeleton);
    ImGui::Checkbox("Snaplines", &config->m_espSnaplines);
    ImGui::Checkbox("Distance", &config->m_espDistance);
    ImGui::Checkbox("Sound ESP", &config->m_espSound);
    ImGui::Checkbox("Teammates", &config->m_espTeammates);
    ImGui::ColorEdit3("Visible Color", (float*)&config->m_espVisibleColor);
    ImGui::ColorEdit3("Hidden Color", (float*)&config->m_espHiddenColor);
    ImGui::Separator();
    ImGui::Checkbox("Chams Enabled", &config->m_chamsEnabled);
    ImGui::Checkbox("Visible Chams", &config->m_chamsVisible);
    ImGui::ColorEdit3("Visible Color", (float*)&config->m_chamsVisibleColor);
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("VisualsRight", ImVec2(0, 0), true);
    ImGui::Checkbox("Hidden Chams", &config->m_chamsHidden);
    ImGui::ColorEdit3("Hidden Color", (float*)&config->m_chamsHiddenColor);
    ImGui::Checkbox("Weapon Chams", &config->m_chamsWeapon);
    ImGui::ColorEdit3("Weapon Color", (float*)&config->m_chamsWeaponColor);
    ImGui::Separator();
    ImGui::Checkbox("Glow Enabled", &config->m_glowEnabled);
    ImGui::ColorEdit3("Glow Color", (float*)&config->m_glowColor);
    ImGui::SliderFloat("Glow Alpha", &config->m_glowAlpha, 0.0f, 1.0f);
    ImGui::Checkbox("Glow Hidden", &config->m_glowHidden);
    ImGui::Separator();
    ImGui::Checkbox("Hit Marker", &config->m_hitMarker);
    ImGui::SliderFloat("Hit Marker Time", &config->m_hitMarkerTime, 0.0f, 2.0f);
    ImGui::Checkbox("Grenade Prediction", &config->m_grenadePrediction);
    ImGui::EndChild();
}

// -----------------------------------------------------------------
// Render misc tab
// -----------------------------------------------------------------
void UIManager::RenderMiscTab() {
    Config* config = g_Cheat->GetConfig();

    // Misc features in columns
    ImGui::BeginChild("MiscLeft", ImVec2(ImGui::GetContentRegionAvail().x * 0.333f, 0), true);
    ImGui::Text("Movement");
    ImGui::Separator();
    ImGui::Checkbox("Bunny Hop",   &config->m_bunnyhop);
    ImGui::Checkbox("Auto Strafe", &config->m_autoStrafe);
    ImGui::Checkbox("Third Person", &config->m_thirdPerson);
    ImGui::SliderFloat("TP Distance", &config->m_thirdPersonDist, 50.f, 300.f, "%.0f");
    ImGui::Spacing();
    ImGui::Text("Combat");
    ImGui::Separator();
    ImGui::Checkbox("No Recoil / No Spread", &config->m_noRecoil);
    ImGui::TextColored(ImVec4(0.5f,1,0.5f,1),"Zeroes punch+spray+accuracy");
    ImGui::Spacing();
    ImGui::Text("Utility");
    ImGui::Separator();
    ImGui::Checkbox("No Flash",    &config->m_noFlash);
    ImGui::Checkbox("Vote Reveal", &config->m_voteReveal);
    ImGui::Checkbox("Auto Accept", &config->m_autoAccept);
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("MiscCenter", ImVec2(ImGui::GetContentRegionAvail().x * 0.333f, 0), true);
    ImGui::Checkbox("HUD Removal", &config->m_hudRemoval);
    ImGui::Checkbox("Skybox Removal", &config->m_skyboxRemoval);
    ImGui::Checkbox("Shadow Removal", &config->m_shadowRemoval);
    ImGui::Checkbox("Scope Removal", &config->m_scopeRemoval);
    ImGui::Checkbox("Fog Removal", &config->m_fogRemoval);
    ImGui::Checkbox("Smoke Removal", &config->m_smokeRemoval);
    ImGui::Checkbox("Flash Reduction", &config->m_flashReduction);
    ImGui::SliderFloat("Flash Amount", &config->m_flashAmount, 0.0f, 100.0f);
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("MiscRight", ImVec2(0, 0), true);
    ImGui::Checkbox("Chat Spam Block", &config->m_chatSpamBlock);
    ImGui::Checkbox("Message Filter", &config->m_messageFilter);
    ImGui::Checkbox("Auto Pistol", &config->m_autoPistol);
    ImGui::Checkbox("Auto Reload", &config->m_autoReload);
    ImGui::EndChild();
}

// -----------------------------------------------------------------
// Render config tab
// -----------------------------------------------------------------
void UIManager::RenderConfigTab() {
    Config* config = g_Cheat->GetConfig();

    static char configName[64] = "default";

    ImGui::InputText("Config Name", configName, sizeof(configName));
    if (ImGui::Button("Save")) {
        config->Save(configName);
    }
    ImGui::SameLine();
    if (ImGui::Button("Load")) {
        config->Load(configName);
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete")) {
        config->Delete(configName);
    }

    ImGui::Separator();
    ImGui::Text("Available Configs:");

    auto configs = config->GetList();
    for (auto& cfg : configs) {
        if (ImGui::Selectable(cfg.c_str())) {
            strcpy_s(configName, sizeof(configName), cfg.c_str());
        }
    }

    ImGui::Separator();
    if (ImGui::Button("Export")) {
        config->Export(configName);
    }
    ImGui::SameLine();
    if (ImGui::Button("Import")) {
        config->Import(configName);
    }
}

// -----------------------------------------------------------------
// Render lua tab
// -----------------------------------------------------------------
void UIManager::RenderLuaTab() {
    ImGui::Text("Loaded Scripts:");
    ImGui::Separator();

    if (ImGui::Button("Load Script")) {
        // Open file dialog
    }
    ImGui::SameLine();
    if (ImGui::Button("Reload All")) {
        g_Cheat->GetLua()->ReloadAll();
    }
    ImGui::SameLine();
    if (ImGui::Button("Open Workshop")) {
        ShellExecuteA(NULL, "open", "https://neverlose.cc/workshop/", NULL, NULL, SW_SHOW);
    }
}