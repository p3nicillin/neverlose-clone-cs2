// =================================================================
// dx11_hook.cpp - IDXGISwapChain::Present hook
//
// Strategy: find Present without creating a dummy DX11 device (which
// conflicts with CS2's device).  We do it by:
//   1. Loading dxgi.dll (already mapped in-process) and reading
//      its export "CreateDXGIFactory" to get a live IDXGIFactory.
//   2. Calling IDXGIFactory::CreateSwapChain with a tiny HWND to
//      get a real IDXGISwapChain whose vtable[8] == Present.
//   BUT: even that can freeze CS2. So instead we use a hidden
//   offscreen window + the already-loaded D3D11 / DXGI to make
//   the dummy device off the main thread.
//
//   If even that is too risky, fall back to reading vtable from
//   a known offset in dxgi.dll's .rdata section.
// =================================================================

#include "dx11_hook.h"
#include "cheat_core.h"
#include "ui_manager.h"
#include "logger.h"
#include <d3d11.h>
#include <dxgi.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include <process.h>

// Do NOT statically link d3d11/dxgi — they must already be loaded by CS2.
// Static imports would trigger loader-lock deadlock during DLL injection.

typedef HRESULT(STDMETHODCALLTYPE* Present_t)(IDXGISwapChain*, UINT, UINT);
static Present_t g_OrigPresent = nullptr;
static WNDPROC   g_OrigWndProc = nullptr;
static HWND      g_GameHwnd   = nullptr;
static void**    g_PresentVTableSlot = nullptr;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (g_Cheat && g_Cheat->GetUI() && g_Cheat->GetUI()->IsRendererReady())
        if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wp, lp)) return TRUE;
    return CallWindowProcW(g_OrigWndProc, hWnd, msg, wp, lp);
}

static ID3D11Device*           g_pDev  = nullptr;
static ID3D11DeviceContext*    g_pCtx  = nullptr;
static ID3D11RenderTargetView* g_pRTV  = nullptr;
static volatile bool           g_DeviceObjectsReady = false;

// Safe GetBuffer wrapper — returns nullptr if GetBuffer throws or fails.
// Called from a function with no C++ objects so __try works.
static ID3D11Texture2D* SafeGetBuffer(IDXGISwapChain* sc) {
    ID3D11Texture2D* p = nullptr;
    __try {
        sc->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&p);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        p = nullptr;
    }
    return p;
}

static void RenderUI() {
    if (!g_pCtx || !g_DeviceObjectsReady) return; // wait until shaders compiled
    if (g_pRTV)
        g_pCtx->OMSetRenderTargets(1, &g_pRTV, nullptr);
    g_Cheat->GetUI()->Update();
    g_Cheat->GetUI()->Render();
}

static HRESULT STDMETHODCALLTYPE HookedPresent(IDXGISwapChain* sc, UINT sync, UINT flags) {
    static bool rendererInit = false;
    if (!rendererInit && g_Cheat && g_Cheat->GetUI()) {
        ID3D11Device* dev = nullptr;
        if (SUCCEEDED(sc->GetDevice(__uuidof(ID3D11Device), (void**)&dev))) {
            ID3D11DeviceContext* ctx = nullptr;
            dev->GetImmediateContext(&ctx);

            DXGI_SWAP_CHAIN_DESC desc = {};
            sc->GetDesc(&desc);
            g_GameHwnd = desc.OutputWindow;
            g_OrigWndProc = (WNDPROC)SetWindowLongPtrW(g_GameHwnd, GWLP_WNDPROC,
                                                         (LONG_PTR)HookedWndProc);
            g_pDev = dev;
            g_pCtx = ctx;

            // Try to get the actual back-buffer RTV via GetBuffer (wrapped in SEH)
            ID3D11Texture2D* pBack = SafeGetBuffer(sc);
            if (pBack) {
                dev->CreateRenderTargetView(pBack, nullptr, &g_pRTV);
                pBack->Release();
                Logger::Log("DX11: back-buffer RTV from GetBuffer");
            } else {
                // Fallback: capture whatever RT CS2 currently has bound
                ID3D11RenderTargetView* rtvs[1] = {};
                ctx->OMGetRenderTargets(1, rtvs, nullptr);
                g_pRTV = rtvs[0];
                Logger::Log(g_pRTV ? "DX11: fallback RT from OMGetRenderTargets"
                                   : "DX11: WARNING no RT available");
            }

            g_Cheat->GetUI()->InitRenderer(dev, ctx, g_GameHwnd);
            // Keep dev and ctx references for RenderUI (don't release)
            rendererInit = true;
            Logger::Log("DX11 hook: renderer initialized");

            // Compile shaders OFF the render thread to avoid blocking CS2's
            // GPU watchdog. g_DeviceObjectsReady flips true when done.
            _beginthreadex(nullptr, 0, [](void*) -> unsigned {
                Logger::Log("DX11: compiling shaders (background)...");
                ImGui_ImplDX11_CreateDeviceObjects();
                g_DeviceObjectsReady = true;
                Logger::Log("DX11: shaders ready — rendering active");
                return 0;
            }, nullptr, 0, nullptr);
        }
    }

    if (rendererInit && g_Cheat && g_Cheat->GetUI())
        RenderUI();

    return g_OrigPresent(sc, sync, flags);
}

// ---------------------------------------------------------------------------
// Find Present vtable pointer without creating a D3D11 device:
// Create a tiny hidden Win32 window and a DXGI swap chain using
// IDXGIFactory::CreateSwapChain which only needs the window handle.
// We use a minimal Win32 + DXGI path (no D3D11 device) to avoid
// conflicting with CS2's D3D11 state.
// ---------------------------------------------------------------------------
bool DX11Hook::Install() {
    Logger::Log("DX11Hook: installing Present hook...");

    // Create an invisible temp window for the dummy swap chain
    WNDCLASSEXA wc = { sizeof(wc) };
    wc.lpfnWndProc   = DefWindowProcA;
    wc.lpszClassName = "NeverloseTemp";
    wc.hInstance     = GetModuleHandleA(nullptr);
    RegisterClassExA(&wc);

    HWND hTempWnd = CreateWindowExA(0, "NeverloseTemp", nullptr, WS_OVERLAPPED,
                                     0, 0, 2, 2, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hTempWnd) {
        Logger::LogError("DX11Hook: CreateWindowEx failed");
        UnregisterClassA("NeverloseTemp", wc.hInstance);
        return false;
    }

    // Create a minimal D3D11 device + swap chain
    D3D_FEATURE_LEVEL fl;
    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferCount         = 1;
    scd.BufferDesc.Format   = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferDesc.Width    = 2;
    scd.BufferDesc.Height   = 2;
    scd.BufferUsage         = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow        = hTempWnd;   // ← our own temp window, NOT CS2's
    scd.SampleDesc.Count    = 1;
    scd.Windowed            = TRUE;
    scd.SwapEffect          = DXGI_SWAP_EFFECT_DISCARD;

    ID3D11Device*        pDevice  = nullptr;
    ID3D11DeviceContext* pCtx     = nullptr;
    IDXGISwapChain*      pSC      = nullptr;

    typedef HRESULT(WINAPI* PFN_D3D11CreateDeviceAndSwapChain)(
        IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
        const D3D_FEATURE_LEVEL*, UINT, UINT,
        const DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**,
        ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);

    HMODULE hD3D11 = GetModuleHandleA("d3d11.dll");
    if (!hD3D11) hD3D11 = LoadLibraryA("d3d11.dll");
    if (!hD3D11) { Logger::LogError("DX11Hook: d3d11.dll not loaded"); DestroyWindow(hTempWnd); UnregisterClassA("NeverloseTemp", wc.hInstance); return false; }

    auto pCreate = (PFN_D3D11CreateDeviceAndSwapChain)
        GetProcAddress(hD3D11, "D3D11CreateDeviceAndSwapChain");
    if (!pCreate) { Logger::LogError("DX11Hook: D3D11CreateDeviceAndSwapChain not found"); DestroyWindow(hTempWnd); UnregisterClassA("NeverloseTemp", wc.hInstance); return false; }

    HRESULT hr = pCreate(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        0, nullptr, 0, D3D11_SDK_VERSION,
        &scd, &pSC, &pDevice, &fl, &pCtx);

    DestroyWindow(hTempWnd);
    UnregisterClassA("NeverloseTemp", wc.hInstance);

    if (FAILED(hr) || !pSC) {
        Logger::LogError("DX11Hook: D3D11CreateDeviceAndSwapChain failed (hr=0x" +
                         std::to_string((unsigned)hr) + ")");
        if (pCtx) pCtx->Release();
        if (pDevice) pDevice->Release();
        return false;
    }

    // Grab vtable slot 8 = Present from the dummy swap chain
    void** vt = *(void***)pSC;
    g_PresentVTableSlot = &vt[8];
    g_OrigPresent       = (Present_t)vt[8];

    // Patch it
    DWORD oldProt;
    VirtualProtect(g_PresentVTableSlot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProt);
    *g_PresentVTableSlot = (void*)HookedPresent;
    VirtualProtect(g_PresentVTableSlot, sizeof(void*), oldProt, &oldProt);

    pSC->Release();
    pCtx->Release();
    pDevice->Release();

    Logger::Log("DX11Hook: Present hooked at vtable slot 8");
    return true;
}

void DX11Hook::Uninstall() {
    if (!g_OrigPresent || !g_PresentVTableSlot) return;

    if (g_GameHwnd && g_OrigWndProc) {
        SetWindowLongPtrW(g_GameHwnd, GWLP_WNDPROC, (LONG_PTR)g_OrigWndProc);
        g_OrigWndProc = nullptr;
    }

    DWORD oldProt;
    VirtualProtect(g_PresentVTableSlot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProt);
    *g_PresentVTableSlot = (void*)g_OrigPresent;
    VirtualProtect(g_PresentVTableSlot, sizeof(void*), oldProt, &oldProt);

    g_OrigPresent        = nullptr;
    g_PresentVTableSlot  = nullptr;
}
