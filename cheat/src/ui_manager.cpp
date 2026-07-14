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
void UIManager::Render() {
    if (!m_initialized || !m_rendererReady) return;

    // Render primary cheat visuals
    if (g_Cheat && g_Cheat->GetVisuals()) {
        g_Cheat->GetVisuals()->Render();
    }

    // Premium Neverlose.cc Watermark
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    if (g_Cheat && g_Cheat->GetConfig() && g_Cheat->GetConfig()->m_visualsEnabled) {
        char watermark[128];
        int fps = (int)ImGui::GetIO().Framerate;
        sprintf_s(watermark, "neverlose.cc | CS2 | FPS: %d | premium", fps);
        ImVec2 textSz = ImGui::GetFont()->CalcTextSizeA(13.f, FLT_MAX, 0.f, watermark);
        float wWidth = textSz.x + 20.f;
        float wHeight = 24.f;
        float wx = ImGui::GetIO().DisplaySize.x - wWidth - 15.f;
        float wy = 15.f;

        // Draw sleek semi-transparent box
        dl->AddRectFilled(ImVec2(wx, wy), ImVec2(wx + wWidth, wy + wHeight), IM_COL32(18, 18, 22, 230), 4.f);
        dl->AddRect(ImVec2(wx, wy), ImVec2(wx + wWidth, wy + wHeight), IM_COL32(0, 200, 255, 200), 4.f);
        dl->AddText(ImVec2(wx + 10.f, wy + (wHeight - textSz.y) * 0.5f), IM_COL32(255, 255, 255, 255), watermark);

        // Aimbot FOV Circle around crosshair
        Config* cfg = g_Cheat->GetConfig();
        if (cfg->m_aimbotEnabled && cfg->m_aimbotFov > 0.1f) {
            float cx = ImGui::GetIO().DisplaySize.x * 0.5f;
            float cy = ImGui::GetIO().DisplaySize.y * 0.5f;
            // Approximate translation from FOV degrees to screen pixels (using typical 90 FOV ratio)
            float radius = (cfg->m_aimbotFov / 90.f) * (ImGui::GetIO().DisplaySize.x * 0.5f);
            dl->AddCircle(ImVec2(cx, cy), radius, IM_COL32(0, 200, 255, 100), 64, 1.f);
        }
    }

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
    ImGui::Checkbox("No Recoil", &config->m_noRecoil);
    ImGui::Checkbox("No Spread", &config->m_noSpread);
    ImGui::TextColored(ImVec4(0.5f,1,0.5f,1),"Separate recoil and spread controls");
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
