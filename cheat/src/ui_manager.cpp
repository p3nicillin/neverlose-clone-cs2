// =================================================================
// ui_manager.cpp - UI Manager implementation
// =================================================================

#include "ui_manager.h"
#include "logger.h"
#include "config.h"
#include <imgui.h>
#include <imgui_impl_win32.h>

// Global UI manager instance
UIManager* g_UIManager = nullptr;

// -----------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------
UIManager::UIManager()
    : m_initialized(false)
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
void UIManager::Update() {
    if (!m_initialized) {
        return;
    }

    ImGui::GetIO().DeltaTime = 1.0f / 60.0f;

    // Start new frame
    ImGui::NewFrame();

    // Handle input
    HandleInput();
}

// -----------------------------------------------------------------
// Render UI
// -----------------------------------------------------------------
void UIManager::Render() {
    if (!m_initialized) {
        return;
    }

    // Render menu
    if (m_menuOpen) {
        RenderMenu();
    }

    // Render ImGui
    ImGui::Render();
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
    style.Colors[ImGuiCol_TabActive] = ImColor(0, 200, 255, 255);
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
    ImGui::Begin("Neverlose.cc", &m_menuOpen, 
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);

    // Tabs
    if (ImGui::BeginTabBar("MainTabs")) {
        if (ImGui::BeginTabItem("Ragebot")) {
            RenderRagebotTab();
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

    ImGui::BeginChild("RagebotLeft", ImVec2(ImGui::GetWindowContentRegionWidth() * 0.5f, 0), true);
    ImGui::Checkbox("Ragebot Enabled", &config->m_ragebotEnabled);
    ImGui::SliderFloat("FOV", &config->m_ragebotFOV, 0.0f, 180.0f);
    ImGui::SliderFloat("Smooth", &config->m_ragebotSmooth, 0.0f, 10.0f);
    ImGui::SliderFloat("Hitchance", &config->m_ragebotHitchance, 0.0f, 100.0f);
    ImGui::SliderFloat("Min Damage", &config->m_ragebotMinDamage, 0.0f, 100.0f);
    ImGui::Checkbox("Auto Fire", &config->m_ragebotAutoFire);
    ImGui::Checkbox("Auto Stop", &config->m_ragebotAutoStop);
    ImGui::Checkbox("Extrapolation", &config->m_ragebotExtrapolation);
    ImGui::Checkbox("Backtrack", &config->m_ragebotBacktrack);
    ImGui::SliderFloat("Backtrack Time", &config->m_ragebotBacktrackTime, 0.0f, 0.5f);
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
// Render anti-aim tab
// -----------------------------------------------------------------
void UIManager::RenderAntiAimTab() {
    Config* config = g_Cheat->GetConfig();

    ImGui::BeginChild("AntiAimLeft", ImVec2(ImGui::GetWindowContentRegionWidth() * 0.5f, 0), true);
    ImGui::Checkbox("Anti-Aim Enabled", &config->m_antiaimEnabled);
    ImGui::Combo("Mode", &config->m_antiaimMode, 
        "Backward\0Jitter\0Spin\0Sideways\0Desync\0Jitter 3-Way\0Custom\0");
    ImGui::SliderFloat("Spin Speed", &config->m_antiaimSpinSpeed, 1.0f, 20.0f);
    ImGui::Checkbox("Desync", &config->m_antiaimDesync);
    ImGui::SliderFloat("Desync Amount", &config->m_antiaimDesyncAmount, 0.0f, 180.0f);
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
    ImGui::EndChild();
}

// -----------------------------------------------------------------
// Render visuals tab
// -----------------------------------------------------------------
void UIManager::RenderVisualsTab() {
    Config* config = g_Cheat->GetConfig();

    ImGui::BeginChild("VisualsLeft", ImVec2(ImGui::GetWindowContentRegionWidth() * 0.333f, 0), true);
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

    ImGui::BeginChild("VisualsCenter", ImVec2(ImGui::GetWindowContentRegionWidth() * 0.333f, 0), true);
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
    ImGui::BeginChild("MiscLeft", ImVec2(ImGui::GetWindowContentRegionWidth() * 0.333f, 0), true);
    ImGui::Checkbox("Knife Bot", &config->m_knifeBot);
    ImGui::Checkbox("Vote Reveal", &config->m_voteReveal);
    ImGui::Checkbox("Skin Changer", &config->m_skinChanger);
    ImGui::Checkbox("Name Spammer", &config->m_nameSpammer);
    ImGui::Checkbox("Clan Tag Spammer", &config->m_clanTagSpammer);
    ImGui::Checkbox("Auto Accept", &config->m_autoAccept);
    ImGui::Checkbox("Rank Revealer", &config->m_rankRevealer);
    ImGui::Checkbox("Damage Report", &config->m_damageReport);
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("MiscCenter", ImVec2(ImGui::GetWindowContentRegionWidth() * 0.333f, 0), true);
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
            strcpy(configName, cfg.c_str());
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