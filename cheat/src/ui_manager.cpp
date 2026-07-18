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

    // Premium Horizon.cc Watermark
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    if (g_Cheat && g_Cheat->GetConfig() && g_Cheat->GetConfig()->m_visualsEnabled) {
        char watermark[128];
        int fps = (int)ImGui::GetIO().Framerate;
        sprintf_s(watermark, "horizon.cc | CS2 | FPS: %d | premium", fps);
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

    // Rounding & Padding
    style.WindowRounding    = 12.0f;
    style.FrameRounding     = 6.0f;
    style.GrabRounding      = 6.0f;
    style.TabRounding       = 6.0f;
    style.ScrollbarRounding = 8.0f;
    style.ChildRounding     = 8.0f;
    style.PopupRounding     = 8.0f;

    style.WindowPadding     = ImVec2(16.f, 16.f);
    style.FramePadding      = ImVec2(12.f, 6.f);
    style.ItemSpacing       = ImVec2(10.f, 12.f);
    style.ItemInnerSpacing  = ImVec2(8.f, 6.f);

    style.WindowBorderSize  = 1.0f;
    style.FrameBorderSize   = 0.0f;
    style.PopupBorderSize   = 1.0f;
    style.ChildBorderSize   = 1.0f;

    // Palette Colors (Deep dark backgrounds with electric blue/neon cyan accents)
    ImVec4 activeBlue       = ImVec4(0.0f, 0.63f, 1.0f, 1.0f);       // #00A2FF
    ImVec4 hoverBlue        = ImVec4(0.0f, 0.63f, 1.0f, 0.8f);
    ImVec4 darkBg           = ImVec4(0.05f, 0.06f, 0.08f, 0.96f);    // #0d1014
    ImVec4 childBg          = ImVec4(0.08f, 0.09f, 0.11f, 1.0f);     // #14171c
    ImVec4 frameBg          = ImVec4(0.11f, 0.13f, 0.16f, 1.0f);     // #1c2129
    ImVec4 border           = ImVec4(0.16f, 0.20f, 0.25f, 1.0f);     // #293340

    style.Colors[ImGuiCol_Text]                  = ImVec4(0.92f, 0.94f, 0.96f, 1.0f);
    style.Colors[ImGuiCol_TextDisabled]          = ImVec4(0.45f, 0.50f, 0.55f, 1.0f);
    style.Colors[ImGuiCol_WindowBg]              = darkBg;
    style.Colors[ImGuiCol_ChildBg]               = childBg;
    style.Colors[ImGuiCol_PopupBg]               = childBg;
    style.Colors[ImGuiCol_Border]                = border;
    style.Colors[ImGuiCol_BorderShadow]          = ImVec4(0.f, 0.f, 0.f, 0.f);
    style.Colors[ImGuiCol_FrameBg]               = frameBg;
    style.Colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.15f, 0.18f, 0.22f, 1.0f);
    style.Colors[ImGuiCol_FrameBgActive]         = ImVec4(0.20f, 0.24f, 0.30f, 1.0f);
    style.Colors[ImGuiCol_TitleBg]               = darkBg;
    style.Colors[ImGuiCol_TitleBgActive]         = darkBg;
    style.Colors[ImGuiCol_TitleBgCollapsed]      = darkBg;
    style.Colors[ImGuiCol_MenuBarBg]             = childBg;
    style.Colors[ImGuiCol_ScrollbarBg]           = darkBg;
    style.Colors[ImGuiCol_ScrollbarGrab]         = frameBg;
    style.Colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.18f, 0.22f, 0.28f, 1.0f);
    style.Colors[ImGuiCol_ScrollbarGrabActive]   = activeBlue;
    style.Colors[ImGuiCol_CheckMark]             = activeBlue;
    style.Colors[ImGuiCol_SliderGrab]            = activeBlue;
    style.Colors[ImGuiCol_SliderGrabActive]      = hoverBlue;
    style.Colors[ImGuiCol_Button]                = frameBg;
    style.Colors[ImGuiCol_ButtonHovered]         = ImVec4(0.16f, 0.45f, 0.75f, 0.6f);
    style.Colors[ImGuiCol_ButtonActive]          = activeBlue;
    style.Colors[ImGuiCol_Header]                = ImVec4(0.0f, 0.63f, 1.0f, 0.15f);
    style.Colors[ImGuiCol_HeaderHovered]         = ImVec4(0.0f, 0.63f, 1.0f, 0.3f);
    style.Colors[ImGuiCol_HeaderActive]          = ImVec4(0.0f, 0.63f, 1.0f, 0.5f);
    style.Colors[ImGuiCol_Separator]             = border;
    style.Colors[ImGuiCol_SeparatorHovered]      = activeBlue;
    style.Colors[ImGuiCol_SeparatorActive]       = hoverBlue;
    style.Colors[ImGuiCol_ResizeGrip]            = border;
    style.Colors[ImGuiCol_ResizeGripHovered]     = hoverBlue;
    style.Colors[ImGuiCol_ResizeGripActive]      = activeBlue;
    style.Colors[ImGuiCol_Tab]                   = frameBg;
    style.Colors[ImGuiCol_TabHovered]            = ImVec4(0.15f, 0.18f, 0.22f, 1.0f);
    style.Colors[ImGuiCol_TabSelected]           = activeBlue;
    style.Colors[ImGuiCol_TabDimmed]             = frameBg;
    style.Colors[ImGuiCol_TabDimmedSelected]     = activeBlue;
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
    ImGui::SetNextWindowSize(ImVec2(800, 520), ImGuiCond_Once);
    ImGui::SetNextWindowPos(ImVec2(100, 100), ImGuiCond_Once);
    
    // Disable default titlebar border for a super sleek premium look
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.63f, 1.0f, 0.8f)); // Cyan glowing border
    
    // Enable resizing
    ImGui::Begin("Horizon.cc v1.0", &m_menuOpen, ImGuiWindowFlags_NoCollapse);

    // Compute dynamic scaling based on current window size relative to default 800x520
    ImVec2 wSize = ImGui::GetWindowSize();
    float scale = wSize.x / 800.f;
    if (scale < 0.7f) scale = 0.7f;
    if (scale > 2.0f) scale = 2.0f;
    ImGui::GetStyle().FontScaleMain = scale;

    // Track active tab locally
    static int activeTab = 0;

    // Left Column: Sidebar (scales with size)
    ImGui::BeginChild("Sidebar", ImVec2(170.f * scale, 0.f), true);
    
    // Branding
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.0f, 0.63f, 1.0f, 1.0f), "  HORIZON.CC");
    ImGui::TextColored(ImVec4(0.45f, 0.50f, 0.55f, 1.0f), "   CS2 Premium");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Tab drawing lambdas
    auto drawTabButton = [scale](const char* name, int index, int& activeIndex) {
        bool selected = (index == activeIndex);
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.63f, 1.0f, 0.15f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.63f, 1.0f, 0.22f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.63f, 1.0f, 0.3f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.75f, 1.0f, 1.0f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.f, 0.f, 0.f, 0.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.15f, 0.18f, 0.22f, 0.25f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.18f, 0.22f, 0.45f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.88f, 0.92f, 1.0f));
        }

        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.12f, 0.5f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f * scale);

        if (ImGui::Button(name, ImVec2(ImGui::GetContentRegionAvail().x, 38.f * scale))) {
            activeIndex = index;
        }

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(4);
        ImGui::Spacing();
    };

    drawTabButton("Ragebot",   0, activeTab);
    drawTabButton("Legitbot",   1, activeTab);
    drawTabButton("Anti-Aim",   2, activeTab);
    drawTabButton("Visuals",    3, activeTab);
    drawTabButton("Misc",       4, activeTab);
    drawTabButton("Config",     5, activeTab);
    drawTabButton("Lua Engine", 6, activeTab);

    // Sidebar bottom section: User Card
    float availY = ImGui::GetContentRegionAvail().y;
    if (availY > 60.f) {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + availY - 60.f);
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.45f, 0.50f, 0.55f, 1.0f), "  User: p3nicillin");
        ImGui::TextColored(ImVec4(0.0f, 0.75f, 0.35f, 1.0f), "  Sub: Lifetime");
    }

    ImGui::EndChild();

    ImGui::SameLine();

    // Right Column: Tab Content
    ImGui::BeginChild("ContentArea", ImVec2(0.f, 0.f), false);
    
    // Add header to content area
    const char* tabHeaders[] = { "Rage Aimbot Core", "Legit Aimbot & Recoil Control", "Anti-Aim & Desync Controls", "Esp / Visual Settings", "Miscellaneous Utilities", "Configuration Files", "Lua Developer Workshop" };
    ImGui::TextColored(ImVec4(0.0f, 0.63f, 1.0f, 1.0f), tabHeaders[activeTab]);
    ImGui::Separator();
    ImGui::Spacing();

    switch (activeTab) {
        case 0: RenderRagebotTab(); break;
        case 1: RenderLegitbotTab(); break;
        case 2: RenderAntiAimTab(); break;
        case 3: RenderVisualsTab(); break;
        case 4: RenderMiscTab(); break;
        case 5: RenderConfigTab(); break;
        case 6: RenderLuaTab(); break;
    }

    ImGui::EndChild();

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    ImGui::GetStyle().FontScaleMain = 1.0f;
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
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.50f);
    ImGui::Checkbox("Ragebot Enabled", &config->m_ragebotEnabled);
    ImGui::SliderFloat("FOV", &config->m_ragebotFOV, 0.0f, 180.0f, "%.0f deg");
    // Rage aim is intentionally instant; smoothing belongs to legitbot.
    config->m_ragebotSmooth = 1.0f;
    ImGui::SliderFloat("Hitchance", &config->m_ragebotHitchance, 0.0f, 100.0f, "%.0f%%");
    ImGui::SliderFloat("Min Damage", &config->m_ragebotMinDamage, 0.0f, 100.0f, "%.0f hp");
    ImGui::Checkbox("Auto Fire", &config->m_ragebotAutoFire);
    ImGui::Checkbox("Auto Stop", &config->m_ragebotAutoStop);
    ImGui::Checkbox("Extrapolation", &config->m_ragebotExtrapolation);
    ImGui::Checkbox("Backtrack", &config->m_ragebotBacktrack);
    ImGui::SliderFloat("Backtrack Time", &config->m_ragebotBacktrackTime, 0.0f, 0.5f, "%.2f s");
    ImGui::PopItemWidth();
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("RagebotRight", ImVec2(0, 0), true);
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.50f);
    ImGui::Checkbox("Quick Scope", &config->m_ragebotQuickScope);
    ImGui::Checkbox("Silent Aimbot", &config->m_ragebotSilentAimbot);
    ImGui::Checkbox("Visual Aimbot", &config->m_ragebotVisualAimbot);
    ImGui::Checkbox("Visible only", &config->m_ragebotVisibleCheck);
    ImGui::Checkbox("Leg Movement", &config->m_ragebotLegMovement);
    ImGui::Checkbox("Multipoint", &config->m_ragebotMultipoint);
    ImGui::SliderFloat("Multipoint Scale", &config->m_ragebotMultipointScale, 0.0f, 1.0f);
    ImGui::Checkbox("Resolver", &config->m_ragebotResolver);
    ImGui::Combo("Resolver Mode", &config->m_ragebotResolverMode, "Auto\0LBY\0History\0");
    ImGui::Separator();
    ImGui::Text("Rage weapon control");
    ImGui::Checkbox("No Recoil", &config->m_ragebotNoRecoil);
    ImGui::Checkbox("No Spread", &config->m_ragebotNoSpread);
    ImGui::PopItemWidth();
    ImGui::EndChild();
}

// -----------------------------------------------------------------
// Legitbot tab — triggerbot + smooth aim
// -----------------------------------------------------------------
void UIManager::RenderLegitbotTab() {
    Config* config = g_Cheat->GetConfig();

    ImGui::BeginChild("LegitLeft", ImVec2(ImGui::GetContentRegionAvail().x * 0.5f, 0), true);
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
    ImGui::Text("Legitbot Compiled Module");
    ImGui::Separator();
    ImGui::Checkbox("Enable Module", &config->m_legitbotEnabled);
    ImGui::Checkbox("Bunny Hop##lb", &config->m_legitbotBunnyHop);
    ImGui::Checkbox("Edge Jump##lb", &config->m_legitbotEdgeJump);
    // Triggerbot lives solely under "Standalone Triggerbot" (right pane) to
    // avoid two competing triggerbots; the legitbot copy was removed.
    ImGui::Checkbox("Auto Pistol##lb", &config->m_legitbotAutoPistol);
    ImGui::Checkbox("Auto Scope##lb", &config->m_legitbotAutoScope);
    ImGui::Checkbox("Quick Stop##lb", &config->m_legitbotQuickStop);
    ImGui::SliderFloat("Quick Stop Speed##lb", &config->m_legitbotQuickStopSpeed, 0.f, 100.f, "%.0f%%");
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.f), "Bhop Key: Space | Triggerbot Key: Mouse Left");
    ImGui::PopItemWidth();
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("LegitRight", ImVec2(0, 0), true);
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
    ImGui::Text("Legit Aimbot");
    ImGui::Separator();
    ImGui::Checkbox("Enabled##la",   &config->m_aimbotEnabled);
    ImGui::SliderFloat("FOV##la",    &config->m_aimbotFov,    0.5f, 20.f, "%.1f deg");
    ImGui::SliderFloat("Smooth##la", &config->m_aimbotSmooth, 2.f,  20.f, "%.1f");
    ImGui::SliderFloat("RCS##la", &config->m_legitbotRcs, 0.f, 100.f, "%.0f%%");
    ImGui::Checkbox("No teamkill##la", &config->m_aimbotTeamcheck);
    ImGui::Checkbox("Visible only##la", &config->m_aimbotVisibleCheck);
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(1,1,0,1), "Hold LMB to activate");
    
    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Text("Standalone Triggerbot");
    ImGui::Separator();
    ImGui::Checkbox("Enabled##trig",  &config->m_triggerbotEnabled);
    ImGui::SliderFloat("FOV##trig",   &config->m_triggerbotFov,   0.5f, 10.f, "%.1f deg");
    ImGui::SliderInt("Delay (ms)##trig", &config->m_triggerbotDelay, 0, 200);
    ImGui::Checkbox("Visible only##trig", &config->m_triggerbotVisibleCheck);
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.5f,1,0.5f,1), "Auto-fires when crosshair on enemy");
    ImGui::PopItemWidth();
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
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.50f);
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
    ImGui::PopItemWidth();
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("AntiAimRight", ImVec2(0, 0), true);
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.50f);
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
    ImGui::PopItemWidth();
    ImGui::EndChild();
}

// -----------------------------------------------------------------
// Render visuals tab
// -----------------------------------------------------------------
void UIManager::RenderVisualsTab() {
    Config* config = g_Cheat->GetConfig();

    ImGui::BeginChild("VisualsLeft", ImVec2(ImGui::GetContentRegionAvail().x * 0.333f, 0), true);
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
    ImGui::Checkbox("Visuals Enabled", &config->m_visualsEnabled);
    ImGui::Separator();
    ImGui::Checkbox("ESP Enabled", &config->m_espEnabled);
    ImGui::Checkbox("Box", &config->m_espBox);
    ImGui::Checkbox("Health Bar", &config->m_espHealthBar);
    ImGui::Checkbox("Armor Bar", &config->m_espArmorBar);
    ImGui::Checkbox("Name", &config->m_espName);
    ImGui::Checkbox("Weapon", &config->m_espWeapon);
    ImGui::Checkbox("Flags", &config->m_espFlags);
    ImGui::PopItemWidth();
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("VisualsCenter", ImVec2(ImGui::GetContentRegionAvail().x * 0.333f, 0), true);
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
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
    ImGui::PopItemWidth();
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("VisualsRight", ImVec2(0, 0), true);
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
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
    ImGui::PopItemWidth();
    ImGui::EndChild();
}

// -----------------------------------------------------------------
// Render misc tab
// -----------------------------------------------------------------
void UIManager::RenderMiscTab() {
    Config* config = g_Cheat->GetConfig();

    // Misc features in columns
    ImGui::BeginChild("MiscLeft", ImVec2(ImGui::GetContentRegionAvail().x * 0.333f, 0), true);
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
    ImGui::Text("Movement");
    ImGui::Separator();
    ImGui::Checkbox("Bunny Hop",   &config->m_bunnyhop);
    ImGui::Checkbox("Auto Strafe", &config->m_autoStrafe);
    ImGui::Checkbox("Third Person", &config->m_thirdPerson);
    ImGui::SliderFloat("TP Distance", &config->m_thirdPersonDist, 50.f, 300.f, "%.0f");
    ImGui::Spacing();
    ImGui::Text("Combat");
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::Text("Utility");
    ImGui::Separator();
    ImGui::Checkbox("No Flash",    &config->m_noFlash);
    ImGui::Checkbox("Vote Reveal", &config->m_voteReveal);
    ImGui::Checkbox("Auto Accept", &config->m_autoAccept);
    ImGui::Checkbox("Knife Bot", &config->m_knifeBot);
    ImGui::Checkbox("Skin Changer", &config->m_skinChanger);
    ImGui::Checkbox("Rank Revealer", &config->m_rankRevealer);
    ImGui::Checkbox("Damage Reports", &config->m_damageReport);
    ImGui::PopItemWidth();
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("MiscCenter", ImVec2(ImGui::GetContentRegionAvail().x * 0.333f, 0), true);
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
    ImGui::Checkbox("HUD Removal", &config->m_hudRemoval);
    ImGui::Checkbox("Skybox Removal", &config->m_skyboxRemoval);
    ImGui::Checkbox("Shadow Removal", &config->m_shadowRemoval);
    ImGui::Checkbox("Scope Removal", &config->m_scopeRemoval);
    ImGui::Checkbox("Fog Removal", &config->m_fogRemoval);
    ImGui::Checkbox("Smoke Removal", &config->m_smokeRemoval);
    ImGui::Checkbox("Flash Reduction", &config->m_flashReduction);
    ImGui::SliderFloat("Flash Amount", &config->m_flashAmount, 0.0f, 100.0f);
    ImGui::PopItemWidth();
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("MiscRight", ImVec2(0, 0), true);
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
    ImGui::Checkbox("Chat Spam Block", &config->m_chatSpamBlock);
    ImGui::Checkbox("Message Filter", &config->m_messageFilter);
    ImGui::Checkbox("Name Spammer", &config->m_nameSpammer);
    ImGui::Checkbox("Clan Tag Spammer", &config->m_clanTagSpammer);
    ImGui::Checkbox("Auto Pistol", &config->m_autoPistol);
    ImGui::Checkbox("Auto Reload", &config->m_autoReload);
    ImGui::PopItemWidth();
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
        ShellExecuteA(NULL, "open", "https://horizon.cc/workshop/", NULL, NULL, SW_SHOW);
    }
}
