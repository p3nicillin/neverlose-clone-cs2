// =================================================================
// ui_manager.h - UI Manager header
// =================================================================

#pragma once

#include <windows.h>
#include <string>

class UIManager {
public:
    UIManager();
    ~UIManager();

    bool Initialize();
    void Shutdown();
    void Update();
    void Render();

    bool IsMenuOpen() const { return m_menuOpen; }
    void SetMenuOpen(bool open) { m_menuOpen = open; }

private:
    void SetupStyle();
    void LoadFonts();
    void HandleInput();
    void RenderMenu();

    void RenderRagebotTab();
    void RenderAntiAimTab();
    void RenderVisualsTab();
    void RenderMiscTab();
    void RenderConfigTab();
    void RenderLuaTab();

    bool m_initialized;
    bool m_menuOpen;
};