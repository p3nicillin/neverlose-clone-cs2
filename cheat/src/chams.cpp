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

bool Chams::Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx) {
    if (s_ready) return true;
    if (!dev || !ctx) return false;

    s_dev = dev;
    s_ctx = ctx;

    if (!CreateFlatShader(dev)) return false;
    if (!CreateColorBuffer(dev)) return false;
    if (!CreateDepthStates(dev)) return false;

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

    // ---- Save original state ----
    ID3D11PixelShader*        origPS  = nullptr;
    ID3D11DepthStencilState*  origDS  = nullptr;
    UINT                      origRef = 0;
    ID3D11BlendState*         origBS  = nullptr;
    float                     origBF[4] = {};
    UINT                      origSM  = 0;

    ctx->PSGetShader(&origPS, nullptr, nullptr);
    ctx->OMGetDepthStencilState(&origDS, &origRef);
    ctx->OMGetBlendState(&origBS, origBF, &origSM);

    // ---- Pass 1: Hidden chams (through walls, depth disabled) ----
    ctx->OMSetDepthStencilState(s_depthDisabled, 0);
    ctx->PSSetShader(s_flatShader, nullptr, 0);
    SetColor(ctx, hidR, hidG, hidB, 1.0f);
    origDraw(ctx, IndexCount, StartIndex, BaseVertex);

    // ---- Pass 2: Visible chams (depth enabled, draws on top of pass 1) ----
    ctx->OMSetDepthStencilState(s_depthEnabled, 0);
    SetColor(ctx, visR, visG, visB, 1.0f);
    origDraw(ctx, IndexCount, StartIndex, BaseVertex);

    // ---- Restore original state ----
    ctx->PSSetShader(origPS, nullptr, 0);
    ctx->OMSetDepthStencilState(origDS, origRef);
    ctx->OMSetBlendState(origBS, origBF, origSM);

    if (origPS) origPS->Release();
    if (origDS) origDS->Release();
    if (origBS) origBS->Release();
}

// =================================================================
// Hooked DrawIndexed
// =================================================================
static void STDMETHODCALLTYPE hkDrawIndexed(ID3D11DeviceContext* ctx, UINT IndexCount, UINT StartIndex, INT BaseVertex) {
    // Early out if chams not ready or disabled
    Config* cfg = (g_Cheat && g_Cheat->GetConfig()) ? g_Cheat->GetConfig() : nullptr;
    if (!s_ready || !cfg || !cfg->m_chamsEnabled) {
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
