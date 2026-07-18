// =================================================================
// chams.cpp — DX11 internal chams via DrawIndexed vtable hook
//
// How it works:
//   1. We hook ID3D11DeviceContext::DrawIndexed (vtable slot 12) and
//      DrawIndexedInstanced (vtable slot 20).
//   2. On each draw call, we check the vertex buffer stride and index
//      count to identify player model geometry.
//   3. For player models, we:
//      a) Disable depth testing, bind flat-color shader → draw (hidden chams)
//      b) Enable depth testing, bind flat-color shader → draw (visible chams)
//      c) Skip the original draw (replace the model's material entirely)
//   4. This gives proper flat-color chams with through-wall visibility.
// =================================================================

#include "chams.h"
#include "logger.h"
#include <cstring>
#include <cmath>
#include <unordered_set>
#include <algorithm>


// ---- D3DCompile dynamic import ----
typedef HRESULT(WINAPI* PFN_D3DCompile)(
    LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, void*,
    LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);

// Forward declaration for ID3DBlob if d3dcompiler.h not included
#ifndef __d3dcommon_h__
struct ID3DBlob : public IUnknown {
    virtual LPVOID STDMETHODCALLTYPE GetBufferPointer() = 0;
    virtual SIZE_T STDMETHODCALLTYPE GetBufferSize() = 0;
};
#endif

// ---- State ----
static ID3D11Device*             s_dev           = nullptr;
static ID3D11DeviceContext*      s_ctx           = nullptr;
static ID3D11PixelShader*        s_flatShader    = nullptr;
static ID3D11Buffer*             s_colorBuffer   = nullptr;
static ID3D11DepthStencilState*  s_depthDisabled = nullptr;
static ID3D11DepthStencilState*  s_depthEnabled  = nullptr;
static ID3D11DepthStencilState*  s_stencilWrite   = nullptr;
static ID3D11DepthStencilState*  s_stencilWriteDepthDisabled = nullptr;
static ID3D11DepthStencilState*  s_stencilMask    = nullptr;
static ID3D11DepthStencilState*  s_stencilMaskDepthEnabled = nullptr;
static ID3D11RasterizerState*    s_rastWireframe  = nullptr;
static ID3D11BlendState*         s_blendNoColorWrite = nullptr;
static bool                      s_ready         = false;
static bool                      s_hooksInstalled = false;
static UINT                      s_playerStride  = 40;

// DrawIndexed vtable hook state
static void** s_pDrawIndexedSlot           = nullptr;
static void** s_pDrawIndexedInstancedSlot  = nullptr;

typedef void(STDMETHODCALLTYPE* DrawIndexedFn)(ID3D11DeviceContext*, UINT, UINT, INT);
typedef void(STDMETHODCALLTYPE* DrawIndexedInstancedFn)(ID3D11DeviceContext*, UINT, UINT, UINT, INT, UINT);

static DrawIndexedFn           s_origDrawIndexed          = nullptr;
static DrawIndexedInstancedFn  s_origDrawIndexedInstanced = nullptr;

// Stride finder
static bool s_strideFinder = false;
static std::unordered_set<uint64_t> s_seenStrides; // (stride << 32) | indexCount packed

// ---- HLSL source for flat-color pixel shader ----
static const char* k_flatShaderHLSL =
    "cbuffer ChamsColor : register(b13) {\n"
    "    float4 chamsColor;\n"
    "};\n"
    "float4 main() : SV_TARGET {\n"
    "    return chamsColor;\n"
    "}\n";

// ---- Forward declarations ----
static void STDMETHODCALLTYPE hkDrawIndexed(ID3D11DeviceContext* ctx, UINT IndexCount, UINT StartIndex, INT BaseVertex);
static void STDMETHODCALLTYPE hkDrawIndexedInstanced(ID3D11DeviceContext* ctx, UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndex, INT BaseVertex, UINT StartInstance);

// Need to include these for the config check
#include "cheat_core.h"
#include "config.h"

// =================================================================
// Initialization
// =================================================================
static bool CreateFlatShader(ID3D11Device* dev) {
    HMODULE hComp = GetModuleHandleA("d3dcompiler_47.dll");
    if (!hComp) hComp = LoadLibraryA("d3dcompiler_47.dll");
    if (!hComp) {
        Logger::LogError("Chams: d3dcompiler_47.dll not available");
        return false;
    }

    auto pfnCompile = (PFN_D3DCompile)GetProcAddress(hComp, "D3DCompile");
    if (!pfnCompile) {
        Logger::LogError("Chams: D3DCompile not found");
        return false;
    }

    ID3DBlob* blob = nullptr;
    ID3DBlob* errors = nullptr;
    HRESULT hr = pfnCompile(
        k_flatShaderHLSL, strlen(k_flatShaderHLSL), nullptr, nullptr, nullptr,
        "main", "ps_4_0", 0, 0, &blob, &errors);
    if (errors) {
        Logger::LogError("Chams: shader compile errors");
        errors->Release();
    }
    if (FAILED(hr) || !blob) {
        Logger::LogError("Chams: shader compilation failed");
        return false;
    }

    hr = dev->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &s_flatShader);
    blob->Release();

    if (FAILED(hr) || !s_flatShader) {
        Logger::LogError("Chams: CreatePixelShader failed");
        return false;
    }

    Logger::Log("Chams: flat shader created");
    return true;
}

static bool CreateColorBuffer(ID3D11Device* dev) {
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth      = 16; // float4 = 16 bytes
    desc.Usage           = D3D11_USAGE_DYNAMIC;
    desc.BindFlags       = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags  = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = dev->CreateBuffer(&desc, nullptr, &s_colorBuffer);
    if (FAILED(hr) || !s_colorBuffer) {
        Logger::LogError("Chams: CreateBuffer for color CB failed");
        return false;
    }
    return true;
}

static bool CreateDepthStates(ID3D11Device* dev) {
    // Depth disabled (for hidden/through-wall chams)
    D3D11_DEPTH_STENCIL_DESC dsdOff = {};
    dsdOff.DepthEnable    = FALSE;
    dsdOff.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dsdOff.DepthFunc      = D3D11_COMPARISON_ALWAYS;
    HRESULT hr = dev->CreateDepthStencilState(&dsdOff, &s_depthDisabled);
    if (FAILED(hr)) { Logger::LogError("Chams: depth-off state failed"); return false; }

    // Depth enabled (for visible chams)
    D3D11_DEPTH_STENCIL_DESC dsdOn = {};
    dsdOn.DepthEnable    = TRUE;
    dsdOn.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dsdOn.DepthFunc      = D3D11_COMPARISON_LESS_EQUAL;
    hr = dev->CreateDepthStencilState(&dsdOn, &s_depthEnabled);
    if (FAILED(hr)) { Logger::LogError("Chams: depth-on state failed"); return false; }

    return true;
}

static bool CreateGlowStates(ID3D11Device* dev) {
    // 1. Stencil write state (write 1 to stencil buffer, depth enabled)
    D3D11_DEPTH_STENCIL_DESC writeDesc = {};
    writeDesc.DepthEnable      = TRUE;
    writeDesc.DepthWriteMask   = D3D11_DEPTH_WRITE_MASK_ALL;
    writeDesc.DepthFunc        = D3D11_COMPARISON_LESS_EQUAL;
    writeDesc.StencilEnable    = TRUE;
    writeDesc.StencilReadMask  = 0xFF;
    writeDesc.StencilWriteMask = 0xFF;
    writeDesc.FrontFace.StencilFailOp      = D3D11_STENCIL_OP_KEEP;
    writeDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
    writeDesc.FrontFace.StencilPassOp      = D3D11_STENCIL_OP_REPLACE;
    writeDesc.FrontFace.StencilFunc        = D3D11_COMPARISON_ALWAYS;
    writeDesc.BackFace = writeDesc.FrontFace;

    HRESULT hr = dev->CreateDepthStencilState(&writeDesc, &s_stencilWrite);
    if (FAILED(hr)) { Logger::LogError("Chams: s_stencilWrite failed"); return false; }

    // 1b. Stencil write state with depth disabled
    D3D11_DEPTH_STENCIL_DESC writeDescNoDepth = writeDesc;
    writeDescNoDepth.DepthEnable = FALSE;
    writeDescNoDepth.DepthFunc   = D3D11_COMPARISON_ALWAYS;
    hr = dev->CreateDepthStencilState(&writeDescNoDepth, &s_stencilWriteDepthDisabled);
    if (FAILED(hr)) { Logger::LogError("Chams: s_stencilWriteDepthDisabled failed"); return false; }

    // 2. Stencil mask state (only draw where stencil is not 1, depth test disabled for wallhack outline)
    D3D11_DEPTH_STENCIL_DESC maskDesc = {};
    maskDesc.DepthEnable      = FALSE;
    maskDesc.DepthWriteMask   = D3D11_DEPTH_WRITE_MASK_ZERO;
    maskDesc.DepthFunc        = D3D11_COMPARISON_ALWAYS;
    maskDesc.StencilEnable    = TRUE;
    maskDesc.StencilReadMask  = 0xFF;
    maskDesc.StencilWriteMask = 0x00;
    maskDesc.FrontFace.StencilFailOp      = D3D11_STENCIL_OP_KEEP;
    maskDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
    maskDesc.FrontFace.StencilPassOp      = D3D11_STENCIL_OP_KEEP;
    maskDesc.FrontFace.StencilFunc        = D3D11_COMPARISON_NOT_EQUAL;
    maskDesc.BackFace = maskDesc.FrontFace;

    hr = dev->CreateDepthStencilState(&maskDesc, &s_stencilMask);
    if (FAILED(hr)) { Logger::LogError("Chams: s_stencilMask failed"); return false; }

    // 2b. Stencil mask state with depth enabled
    D3D11_DEPTH_STENCIL_DESC maskDescDepth = maskDesc;
    maskDescDepth.DepthEnable = TRUE;
    maskDescDepth.DepthFunc   = D3D11_COMPARISON_LESS_EQUAL;
    hr = dev->CreateDepthStencilState(&maskDescDepth, &s_stencilMaskDepthEnabled);
    if (FAILED(hr)) { Logger::LogError("Chams: s_stencilMaskDepthEnabled failed"); return false; }

    // 3. Rasterizer state for wireframe line drawing
    D3D11_RASTERIZER_DESC rastDesc = {};
    rastDesc.FillMode = D3D11_FILL_WIREFRAME;
    rastDesc.CullMode = D3D11_CULL_NONE;
    rastDesc.DepthClipEnable = TRUE;
    rastDesc.MultisampleEnable = TRUE;
    rastDesc.AntialiasedLineEnable = TRUE;

    hr = dev->CreateRasterizerState(&rastDesc, &s_rastWireframe);
    if (FAILED(hr)) { Logger::LogError("Chams: s_rastWireframe failed"); return false; }

    // 4. Blend state for disabling color writes
    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = FALSE;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = 0; // no color writing

    hr = dev->CreateBlendState(&blendDesc, &s_blendNoColorWrite);
    if (FAILED(hr)) { Logger::LogError("Chams: s_blendNoColorWrite failed"); return false; }

    return true;
}

bool Chams::Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx) {
    if (s_ready) return true;
    if (!dev || !ctx) return false;

    s_dev = dev;
    s_ctx = ctx;

    if (!CreateFlatShader(dev)) return false;
    if (!CreateColorBuffer(dev)) return false;
    if (!CreateDepthStates(dev)) return false;
    if (!CreateGlowStates(dev)) return false;

    s_ready = true;
    Logger::Log("Chams: initialized successfully");
    return true;
}

void Chams::Shutdown() {
    UninstallHooks();
    if (s_flatShader)    { s_flatShader->Release();    s_flatShader = nullptr; }
    if (s_colorBuffer)   { s_colorBuffer->Release();   s_colorBuffer = nullptr; }
    if (s_depthDisabled) { s_depthDisabled->Release();  s_depthDisabled = nullptr; }
    if (s_depthEnabled)  { s_depthEnabled->Release();   s_depthEnabled = nullptr; }
    if (s_stencilWrite)  { s_stencilWrite->Release();  s_stencilWrite = nullptr; }
    if (s_stencilWriteDepthDisabled) { s_stencilWriteDepthDisabled->Release(); s_stencilWriteDepthDisabled = nullptr; }
    if (s_stencilMask)   { s_stencilMask->Release();   s_stencilMask = nullptr; }
    if (s_stencilMaskDepthEnabled) { s_stencilMaskDepthEnabled->Release(); s_stencilMaskDepthEnabled = nullptr; }
    if (s_rastWireframe) { s_rastWireframe->Release(); s_rastWireframe = nullptr; }
    if (s_blendNoColorWrite) { s_blendNoColorWrite->Release(); s_blendNoColorWrite = nullptr; }
    s_ready = false;
    s_dev = nullptr;
    s_ctx = nullptr;
}

bool Chams::IsReady() { return s_ready && s_hooksInstalled; }

// =================================================================
// DrawIndexed vtable hooks
// =================================================================
bool Chams::InstallHooks(ID3D11DeviceContext* ctx) {
    if (s_hooksInstalled) return true;
    if (!ctx) return false;

    void** vtable = *(void***)ctx;
    if (!vtable) return false;

    // DrawIndexed = vtable[12], DrawIndexedInstanced = vtable[20]
    s_origDrawIndexed          = (DrawIndexedFn)vtable[12];
    s_origDrawIndexedInstanced = (DrawIndexedInstancedFn)vtable[20];
    s_pDrawIndexedSlot         = &vtable[12];
    s_pDrawIndexedInstancedSlot = &vtable[20];

    DWORD oldProt;
    VirtualProtect(s_pDrawIndexedSlot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProt);
    *s_pDrawIndexedSlot = (void*)hkDrawIndexed;
    VirtualProtect(s_pDrawIndexedSlot, sizeof(void*), oldProt, &oldProt);

    VirtualProtect(s_pDrawIndexedInstancedSlot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProt);
    *s_pDrawIndexedInstancedSlot = (void*)hkDrawIndexedInstanced;
    VirtualProtect(s_pDrawIndexedInstancedSlot, sizeof(void*), oldProt, &oldProt);

    s_hooksInstalled = true;
    Logger::Log("Chams: DrawIndexed hooks installed (vtable[12], vtable[20])");
    return true;
}

void Chams::UninstallHooks() {
    if (!s_hooksInstalled) return;

    if (s_pDrawIndexedSlot && s_origDrawIndexed) {
        DWORD oldProt;
        VirtualProtect(s_pDrawIndexedSlot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProt);
        *s_pDrawIndexedSlot = (void*)s_origDrawIndexed;
        VirtualProtect(s_pDrawIndexedSlot, sizeof(void*), oldProt, &oldProt);
    }
    if (s_pDrawIndexedInstancedSlot && s_origDrawIndexedInstanced) {
        DWORD oldProt;
        VirtualProtect(s_pDrawIndexedInstancedSlot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProt);
        *s_pDrawIndexedInstancedSlot = (void*)s_origDrawIndexedInstanced;
        VirtualProtect(s_pDrawIndexedInstancedSlot, sizeof(void*), oldProt, &oldProt);
    }

    s_hooksInstalled = false;
    Logger::Log("Chams: DrawIndexed hooks uninstalled");
}

// =================================================================
// Model identification
// =================================================================
bool Chams::IsPlayerModel(UINT stride, UINT indexCount) {
    // CS2 player model heuristic:
    //   stride 40 (pos+normal+texcoord) with 3000..65000 indices
    // This is tunable via SetPlayerStride().
    // Also accept stride 52 for some model variants.
    if (stride != s_playerStride && stride != 52 && stride != 44)
        return false;
    if (indexCount < 3000 || indexCount > 65000)
        return false;
    return true;
}

bool Chams::IsWeaponModel(UINT stride, UINT indexCount) {
    // Weapons and viewmodels (arms/sleeves/weapons) in CS2 typically use:
    //   stride 40 or 44 or 52
    //   indexCount between 500 and 25000
    if (stride != s_playerStride && stride != 44 && stride != 52)
        return false;
    if (indexCount < 500 || indexCount > 25000)
        return false;
    // Avoid classifying player models as weapons
    if (IsPlayerModel(stride, indexCount))
        return false;
    return true;
}

void Chams::SetPlayerStride(UINT stride) { s_playerStride = stride; }
UINT Chams::GetPlayerStride() { return s_playerStride; }

// =================================================================
// Stride finder (debug tool)
// =================================================================
void Chams::EnableStrideFinder(bool enable) { s_strideFinder = enable; }

void Chams::FrameReset() {
    if (s_strideFinder && !s_seenStrides.empty()) {
        // Log unique (stride, indexCount) pairs
        static DWORD lastLog = 0;
        DWORD now = GetTickCount();
        if (now - lastLog > 5000) { // every 5s
            for (auto packed : s_seenStrides) {
                UINT stride = (UINT)(packed >> 32);
                UINT idxCnt = (UINT)(packed & 0xFFFFFFFF);
                Logger::Log("Chams stride: stride=%u indexCount=%u", stride, idxCnt);
            }
            lastLog = now;
        }
    }
    s_seenStrides.clear();
}

// =================================================================
// Rendering
// =================================================================
static void SetColor(ID3D11DeviceContext* ctx, float r, float g, float b, float a) {
    if (!s_colorBuffer) return;
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(ctx->Map(s_colorBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        float* c = (float*)mapped.pData;
        c[0] = r; c[1] = g; c[2] = b; c[3] = a;
        ctx->Unmap(s_colorBuffer, 0);
    }
    ctx->PSSetConstantBuffers(13, 1, &s_colorBuffer);
}

void Chams::RenderChams(ID3D11DeviceContext* ctx, UINT IndexCount, UINT StartIndex, INT BaseVertex,
                         DrawIndexedFn origDraw,
                         float visR, float visG, float visB,
                         float hidR, float hidG, float hidB) {
    if (!s_flatShader || !s_depthDisabled || !s_depthEnabled) return;

    Config* cfg = (g_Cheat && g_Cheat->GetConfig()) ? g_Cheat->GetConfig() : nullptr;
    if (!cfg) return;

    // ---- Save original state ----
    ID3D11PixelShader*        origPS  = nullptr;
    ID3D11DepthStencilState*  origDS  = nullptr;
    UINT                      origRef = 0;
    ID3D11BlendState*         origBS  = nullptr;
    float                     origBF[4] = {};
    UINT                      origSM  = 0;
    ID3D11RasterizerState*    origRS  = nullptr;
    ID3D11Buffer*             origCB  = nullptr;

    ctx->PSGetShader(&origPS, nullptr, nullptr);
    ctx->OMGetDepthStencilState(&origDS, &origRef);
    ctx->OMGetBlendState(&origBS, origBF, &origSM);
    ctx->RSGetState(&origRS);
    ctx->PSGetConstantBuffers(13, 1, &origCB);

    bool chams = cfg->m_chamsEnabled;
    bool glow = cfg->m_glowEnabled;

    // ---- STEP 1: Stencil Write & Base Chams Passes ----
    if (chams) {
        // Draw hidden pass if enabled
        if (cfg->m_chamsHidden) {
            // Write 1 to stencil, ignore depth (draw through walls)
            ctx->OMSetDepthStencilState(glow ? s_stencilWriteDepthDisabled : s_depthDisabled, 1);
            ctx->PSSetShader(s_flatShader, nullptr, 0);
            SetColor(ctx, hidR, hidG, hidB, 1.0f);
            origDraw(ctx, IndexCount, StartIndex, BaseVertex);
        }

        // Draw visible pass if enabled
        if (cfg->m_chamsVisible) {
            // Write 1 to stencil, use normal depth test
            ctx->OMSetDepthStencilState(glow ? s_stencilWrite : s_depthEnabled, 1);
            ctx->PSSetShader(s_flatShader, nullptr, 0);
            SetColor(ctx, visR, visG, visB, 1.0f);
            origDraw(ctx, IndexCount, StartIndex, BaseVertex);
        } else if (!cfg->m_chamsHidden && glow) {
            // Chams visible is disabled, chams hidden is disabled, but glow is enabled.
            // We need to write to the stencil buffer without drawing any color.
            ctx->OMSetBlendState(s_blendNoColorWrite, origBF, origSM);
            ctx->OMSetDepthStencilState(cfg->m_glowHidden ? s_stencilWriteDepthDisabled : s_stencilWrite, 1);
            origDraw(ctx, IndexCount, StartIndex, BaseVertex);
            ctx->OMSetBlendState(origBS, origBF, origSM);
        }
    } else if (glow) {
        // Chams is disabled, but glow is enabled.
        // We write to the stencil buffer with color writes disabled.
        ctx->OMSetBlendState(s_blendNoColorWrite, origBF, origSM);
        ctx->OMSetDepthStencilState(cfg->m_glowHidden ? s_stencilWriteDepthDisabled : s_stencilWrite, 1);
        origDraw(ctx, IndexCount, StartIndex, BaseVertex);
        ctx->OMSetBlendState(origBS, origBF, origSM);
    }

    // ---- STEP 2: Glow/Outline Pass ----
    if (glow) {
        ImVec4 gv = cfg->m_glowColor.Value;
        float alpha = std::clamp(cfg->m_glowAlpha, 0.0f, 1.0f);

        // Bind wireframe rasterizer to draw lines
        ctx->RSSetState(s_rastWireframe);

        // Bind stencil mask (only pass where stencil != 1)
        ctx->OMSetDepthStencilState(cfg->m_glowHidden ? s_stencilMask : s_stencilMaskDepthEnabled, 1);

        // Bind flat shader and set glow color
        ctx->PSSetShader(s_flatShader, nullptr, 0);
        SetColor(ctx, gv.x, gv.y, gv.z, alpha);

        // Draw wireframe outline
        origDraw(ctx, IndexCount, StartIndex, BaseVertex);
    }

    // ---- Restore original state ----
    ctx->PSSetShader(origPS, nullptr, 0);
    ctx->OMSetDepthStencilState(origDS, origRef);
    ctx->OMSetBlendState(origBS, origBF, origSM);
    ctx->RSSetState(origRS);
    ctx->PSSetConstantBuffers(13, 1, &origCB);

    if (origPS) origPS->Release();
    if (origDS) origDS->Release();
    if (origBS) origBS->Release();
    if (origRS) origRS->Release();
    if (origCB) origCB->Release();
}

// =================================================================
// Hooked DrawIndexed
// =================================================================
static void STDMETHODCALLTYPE hkDrawIndexed(ID3D11DeviceContext* ctx, UINT IndexCount, UINT StartIndex, INT BaseVertex) {
    // Early out if chams/glow/weapon chams not ready or disabled
    Config* cfg = (g_Cheat && g_Cheat->GetConfig()) ? g_Cheat->GetConfig() : nullptr;
    if (!s_ready || !cfg || (!cfg->m_chamsEnabled && !cfg->m_glowEnabled && !cfg->m_chamsWeapon)) {
        s_origDrawIndexed(ctx, IndexCount, StartIndex, BaseVertex);
        return;
    }

    // Get current vertex buffer stride
    ID3D11Buffer* vb = nullptr;
    UINT stride = 0, offset = 0;
    ctx->IAGetVertexBuffers(0, 1, &vb, &stride, &offset);
    if (vb) vb->Release();

    // Stride finder logging
    if (s_strideFinder && stride > 0 && IndexCount > 100) {
        uint64_t packed = ((uint64_t)stride << 32) | (uint64_t)IndexCount;
        s_seenStrides.insert(packed);
    }

    // Check if this draw call is a player model
    if (Chams::IsPlayerModel(stride, IndexCount)) {
        // Convert ImColor to float RGB
        ImVec4 vis = cfg->m_chamsVisibleColor.Value;
        ImVec4 hid = cfg->m_chamsHiddenColor.Value;

        bool drawHidden = cfg->m_chamsHidden;
        float hidR = drawHidden ? hid.x : vis.x;
        float hidG = drawHidden ? hid.y : vis.y;
        float hidB = drawHidden ? hid.z : vis.z;

        Chams::RenderChams(ctx, IndexCount, StartIndex, BaseVertex,
                           s_origDrawIndexed,
                           vis.x, vis.y, vis.z,
                           hidR, hidG, hidB);

        // If chams are disabled, we only drew the glow outline/stencil.
        // We still need to draw the original player model texture.
        if (!cfg->m_chamsEnabled) {
            s_origDrawIndexed(ctx, IndexCount, StartIndex, BaseVertex);
        }
        return; // skip original draw (handled inside or original drawn manually above)
    }
    // Check if this draw call is a weapon / viewmodel
    else if (cfg->m_chamsWeapon && Chams::IsWeaponModel(stride, IndexCount)) {
        ImVec4 wepCol = cfg->m_chamsWeaponColor.Value;
        Chams::RenderChams(ctx, IndexCount, StartIndex, BaseVertex,
                           s_origDrawIndexed,
                           wepCol.x, wepCol.y, wepCol.z,
                           wepCol.x, wepCol.y, wepCol.z);
        return; // skip original draw
    }

    s_origDrawIndexed(ctx, IndexCount, StartIndex, BaseVertex);
}

static void STDMETHODCALLTYPE hkDrawIndexedInstanced(ID3D11DeviceContext* ctx,
    UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndex, INT BaseVertex, UINT StartInstance) {
    // For instanced draws, similar logic but less common for player models
    // Just pass through for now; player models typically use DrawIndexed
    s_origDrawIndexedInstanced(ctx, IndexCountPerInstance, InstanceCount, StartIndex, BaseVertex, StartInstance);
}
