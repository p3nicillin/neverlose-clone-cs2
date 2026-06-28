#pragma once

#include <windows.h>
#include <d3d11.h>
#include <string>

class UIManager {
public:
    UIManager();
    ~UIManager();

    bool Initialize();
    void Shutdown();
    void Update();
    void Render();

    // Called from the DX11 Present hook once we have a real device/context
    bool InitRenderer(ID3D11Device* device, ID3D11DeviceContext* ctx, HWND hwnd);
    bool IsRendererReady() const { return m_rendererReady; }

    bool IsMenuOpen() const { return m_menuOpen; }
    void SetMenuOpen(bool open) { m_menuOpen = open; }

private:
    void SetupStyle();
    void LoadFonts();
    void HandleInput();
    void RenderMenu();

    void RenderESP();
    void RenderRagebotTab();
    void RenderLegitbotTab();
    void RenderAntiAimTab();
    void RenderVisualsTab();
    void RenderMiscTab();
    void RenderConfigTab();
    void RenderLuaTab();

    bool m_initialized;
    bool m_rendererReady;   // true only after DX11 backend is set up
    bool m_menuOpen;
};
