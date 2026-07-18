// =================================================================
// chams.h — DX11 internal chams via DrawIndexed vtable hook
//
// Proper internal chams: hooks ID3D11DeviceContext::DrawIndexed to
// override player model materials with flat-color pixel shader and
// depth stencil state manipulation for visible/hidden rendering.
// =================================================================

#pragma once
#include <d3d11.h>
#include <cstdint>

namespace Chams {
    // Call once after DX11 device/context are available
    bool Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx);
    void Shutdown();
    bool IsReady();

    // Install/uninstall DrawIndexed + DrawIndexedInstanced vtable hooks
    bool InstallHooks(ID3D11DeviceContext* ctx);
    void UninstallHooks();

    // Model stride identification — called from DrawIndexed hook
    bool IsPlayerModel(UINT stride, UINT indexCount);
    bool IsWeaponModel(UINT stride, UINT indexCount);

    // Set the flat-color shader and draw with chams overrides.
    // origFn is the saved original DrawIndexed function pointer.
    typedef void(STDMETHODCALLTYPE* DrawIndexedFn)(ID3D11DeviceContext*, UINT, UINT, INT);
    void RenderChams(ID3D11DeviceContext* ctx, UINT IndexCount, UINT StartIndex, INT BaseVertex,
                     DrawIndexedFn origDraw,
                     float visR, float visG, float visB,
                     float hidR, float hidG, float hidB);

    // Logging utility: when enabled, logs unique (stride, indexCount) combos per frame
    void EnableStrideFinder(bool enable);
    void FrameReset(); // call once per frame to reset stride logging

    // Configurable player model stride (default = 40)
    void SetPlayerStride(UINT stride);
    UINT GetPlayerStride();
}
