// dear imgui: Renderer Backend for DirectX11
// Adapted for ImGui 1.92+ with ImGuiBackendFlags_RendererHasTextures

#include "imgui.h"
#ifndef IMGUI_DISABLE
#include "imgui_impl_dx11.h"
#include "imgui_internal.h"

#include <stdio.h>
#include <d3d11.h>
#include <d3dcompiler.h>
// d3dcompiler loaded dynamically below to avoid loader-lock deadlock on injection

struct ImGui_ImplDX11_Data
{
    ID3D11Device*            pd3dDevice          = nullptr;
    ID3D11DeviceContext*     pd3dDeviceContext   = nullptr;
    ID3D11Buffer*            pVB                 = nullptr;
    ID3D11Buffer*            pIB                 = nullptr;
    ID3D11VertexShader*      pVertexShader       = nullptr;
    ID3D11InputLayout*       pInputLayout        = nullptr;
    ID3D11Buffer*            pVertexConstantBuffer = nullptr;
    ID3D11PixelShader*       pPixelShader        = nullptr;
    ID3D11SamplerState*      pFontSampler        = nullptr;
    ID3D11RasterizerState*   pRasterizerState    = nullptr;
    ID3D11BlendState*        pBlendState         = nullptr;
    ID3D11DepthStencilState* pDepthStencilState  = nullptr;
    int                      VertexBufferSize     = 5000;
    int                      IndexBufferSize      = 10000;
    bool                     DeviceObjectsCreated = false;
};

struct VERTEX_CB { float mvp[4][4]; };

static ImGui_ImplDX11_Data* GetBD() {
    return ImGui::GetCurrentContext() ? (ImGui_ImplDX11_Data*)ImGui::GetIO().BackendRendererUserData : nullptr;
}

// ---------------------------------------------------------------------------
// Texture management (ImGui 1.92 RendererHasTextures API)
// ---------------------------------------------------------------------------
static void CreateTexture(ImGui_ImplDX11_Data* bd, ImTextureData* tex)
{
    IM_ASSERT(tex->Status == ImTextureStatus_WantCreate);
    IM_ASSERT(tex->Format == ImTextureFormat_RGBA32 || tex->Format == ImTextureFormat_Alpha8);

    // Choose DXGI format
    DXGI_FORMAT fmt = (tex->Format == ImTextureFormat_RGBA32)
                      ? DXGI_FORMAT_R8G8B8A8_UNORM
                      : DXGI_FORMAT_R8_UNORM;

    // Create texture
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width            = (UINT)tex->Width;
    desc.Height           = (UINT)tex->Height;
    desc.MipLevels        = 1;
    desc.ArraySize        = 1;
    desc.Format           = fmt;
    desc.SampleDesc.Count = 1;
    desc.Usage            = D3D11_USAGE_DEFAULT;
    desc.BindFlags        = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA sub = {};
    sub.pSysMem          = tex->GetPixels();
    sub.SysMemPitch      = (UINT)(tex->Width * tex->BytesPerPixel);

    ID3D11Texture2D* pTex = nullptr;
    if (FAILED(bd->pd3dDevice->CreateTexture2D(&desc, &sub, &pTex)))
        return;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                    = fmt;
    srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels       = 1;

    ID3D11ShaderResourceView* pSRV = nullptr;
    bd->pd3dDevice->CreateShaderResourceView(pTex, &srvDesc, &pSRV);
    pTex->Release();

    tex->SetTexID((ImTextureID)pSRV);
    tex->SetStatus(ImTextureStatus_OK);
}

static void DestroyTexture(ImTextureData* tex)
{
    if (tex->TexID == ImTextureID_Invalid) return;
    auto* pSRV = (ID3D11ShaderResourceView*)(intptr_t)tex->TexID;
    pSRV->Release();
    tex->SetTexID(ImTextureID_Invalid);
    tex->SetStatus(ImTextureStatus_Destroyed);
}

static void UpdateTextures(ImGui_ImplDX11_Data* bd)
{
    ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
    for (ImTextureData* tex : pio.Textures)
    {
        if (tex->Status == ImTextureStatus_WantCreate)
            CreateTexture(bd, tex);
        else if (tex->Status == ImTextureStatus_WantUpdates)
        {
            // Re-upload changed texture
            if (tex->TexID != ImTextureID_Invalid) {
                auto* pSRV = (ID3D11ShaderResourceView*)(intptr_t)tex->TexID;
                pSRV->Release();
                tex->SetTexID(ImTextureID_Invalid);
            }
            tex->Status = ImTextureStatus_WantCreate;
            CreateTexture(bd, tex);
        }
        else if (tex->Status == ImTextureStatus_WantDestroy)
            DestroyTexture(tex);
    }
}

// ---------------------------------------------------------------------------
// Render state helpers
// ---------------------------------------------------------------------------
static void SetupRenderState(ImDrawData* draw_data, ID3D11DeviceContext* ctx)
{
    ImGui_ImplDX11_Data* bd = GetBD();

    D3D11_VIEWPORT vp = {};
    vp.Width    = draw_data->DisplaySize.x;
    vp.Height   = draw_data->DisplaySize.y;
    vp.MaxDepth = 1.0f;
    ctx->RSSetViewports(1, &vp);

    unsigned int stride = sizeof(ImDrawVert), offset = 0;
    ctx->IASetInputLayout(bd->pInputLayout);
    ctx->IASetVertexBuffers(0, 1, &bd->pVB, &stride, &offset);
    ctx->IASetIndexBuffer(bd->pIB, sizeof(ImDrawIdx)==2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT, 0);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(bd->pVertexShader, nullptr, 0);
    ctx->VSSetConstantBuffers(0, 1, &bd->pVertexConstantBuffer);
    ctx->PSSetShader(bd->pPixelShader, nullptr, 0);
    ctx->PSSetSamplers(0, 1, &bd->pFontSampler);
    ctx->GSSetShader(nullptr, nullptr, 0);
    ctx->HSSetShader(nullptr, nullptr, 0);
    ctx->DSSetShader(nullptr, nullptr, 0);
    ctx->CSSetShader(nullptr, nullptr, 0);
    const float bf[4] = {};
    ctx->OMSetBlendState(bd->pBlendState, bf, 0xffffffff);
    ctx->OMSetDepthStencilState(bd->pDepthStencilState, 0);
    ctx->RSSetState(bd->pRasterizerState);
}

// ---------------------------------------------------------------------------
// RenderDrawData
// ---------------------------------------------------------------------------
void ImGui_ImplDX11_RenderDrawData(ImDrawData* draw_data)
{
    if (draw_data->DisplaySize.x <= 0 || draw_data->DisplaySize.y <= 0) return;

    ImGui_ImplDX11_Data* bd = GetBD();
    ID3D11DeviceContext* ctx = bd->pd3dDeviceContext;

    // Upload texture changes
    UpdateTextures(bd);

    // Grow vertex buffer
    if (!bd->pVB || bd->VertexBufferSize < draw_data->TotalVtxCount) {
        if (bd->pVB) { bd->pVB->Release(); bd->pVB = nullptr; }
        bd->VertexBufferSize = draw_data->TotalVtxCount + 5000;
        D3D11_BUFFER_DESC d = {}; d.Usage = D3D11_USAGE_DYNAMIC;
        d.ByteWidth = (UINT)(bd->VertexBufferSize * sizeof(ImDrawVert));
        d.BindFlags = D3D11_BIND_VERTEX_BUFFER; d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(bd->pd3dDevice->CreateBuffer(&d, nullptr, &bd->pVB))) return;
    }
    // Grow index buffer
    if (!bd->pIB || bd->IndexBufferSize < draw_data->TotalIdxCount) {
        if (bd->pIB) { bd->pIB->Release(); bd->pIB = nullptr; }
        bd->IndexBufferSize = draw_data->TotalIdxCount + 10000;
        D3D11_BUFFER_DESC d = {}; d.Usage = D3D11_USAGE_DYNAMIC;
        d.ByteWidth = (UINT)(bd->IndexBufferSize * sizeof(ImDrawIdx));
        d.BindFlags = D3D11_BIND_INDEX_BUFFER; d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(bd->pd3dDevice->CreateBuffer(&d, nullptr, &bd->pIB))) return;
    }

    // Upload geometry
    D3D11_MAPPED_SUBRESOURCE vtxRes, idxRes;
    if (FAILED(ctx->Map(bd->pVB, 0, D3D11_MAP_WRITE_DISCARD, 0, &vtxRes))) return;
    if (FAILED(ctx->Map(bd->pIB, 0, D3D11_MAP_WRITE_DISCARD, 0, &idxRes))) return;
    auto* vDst = (ImDrawVert*)vtxRes.pData;
    auto* iDst = (ImDrawIdx*)idxRes.pData;
    for (int n = 0; n < draw_data->CmdListsCount; n++) {
        const ImDrawList* cl = draw_data->CmdLists[n];
        memcpy(vDst, cl->VtxBuffer.Data, cl->VtxBuffer.Size * sizeof(ImDrawVert)); vDst += cl->VtxBuffer.Size;
        memcpy(iDst, cl->IdxBuffer.Data, cl->IdxBuffer.Size * sizeof(ImDrawIdx));  iDst += cl->IdxBuffer.Size;
    }
    ctx->Unmap(bd->pVB, 0);
    ctx->Unmap(bd->pIB, 0);

    // Update projection constant buffer
    {
        D3D11_MAPPED_SUBRESOURCE m;
        if (FAILED(ctx->Map(bd->pVertexConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return;
        auto* cb = (VERTEX_CB*)m.pData;
        float L = draw_data->DisplayPos.x, R = L + draw_data->DisplaySize.x;
        float T = draw_data->DisplayPos.y, B = T + draw_data->DisplaySize.y;
        float mvp[4][4] = {
            { 2/(R-L),   0,       0,  0 },
            { 0,         2/(T-B), 0,  0 },
            { 0,         0,       .5, 0 },
            { (R+L)/(L-R),(T+B)/(B-T),.5,1},
        };
        memcpy(cb->mvp, mvp, sizeof(mvp));
        ctx->Unmap(bd->pVertexConstantBuffer, 0);
    }

    // Save DX state
    struct SavedState {
        UINT ScissorCount=D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE, VPCount=D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        D3D11_RECT Scissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
        D3D11_VIEWPORT Viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
        ID3D11RasterizerState* RS=nullptr; ID3D11BlendState* BS=nullptr; float BF[4]{}; UINT SM=0,SR=0;
        ID3D11DepthStencilState* DS=nullptr; ID3D11ShaderResourceView* PSR=nullptr; ID3D11SamplerState* PSS=nullptr;
        ID3D11PixelShader* PS=nullptr; ID3D11VertexShader* VS=nullptr; ID3D11GeometryShader* GS=nullptr;
        UINT PSC=256,VSC=256,GSC=256; ID3D11ClassInstance *PSI[256],*VSI[256],*GSI[256];
        D3D11_PRIMITIVE_TOPOLOGY PT; ID3D11Buffer *IB=nullptr,*VB=nullptr,*VCB=nullptr;
        UINT IBO=0,VBS=0,VBO=0; DXGI_FORMAT IBF; ID3D11InputLayout* IL=nullptr;
    } s;
    ctx->RSGetScissorRects(&s.ScissorCount, s.Scissors);
    ctx->RSGetViewports(&s.VPCount, s.Viewports);
    ctx->RSGetState(&s.RS);
    ctx->OMGetBlendState(&s.BS, s.BF, &s.SM);
    ctx->OMGetDepthStencilState(&s.DS, &s.SR);
    ctx->PSGetShaderResources(0,1,&s.PSR); ctx->PSGetSamplers(0,1,&s.PSS);
    ctx->PSGetShader(&s.PS, s.PSI, &s.PSC);
    ctx->VSGetShader(&s.VS, s.VSI, &s.VSC); ctx->VSGetConstantBuffers(0,1,&s.VCB);
    ctx->GSGetShader(&s.GS, s.GSI, &s.GSC);
    ctx->IAGetPrimitiveTopology(&s.PT);
    ctx->IAGetIndexBuffer(&s.IB, &s.IBF, &s.IBO);
    ctx->IAGetVertexBuffers(0,1,&s.VB,&s.VBS,&s.VBO);
    ctx->IAGetInputLayout(&s.IL);

    SetupRenderState(draw_data, ctx);

    int globalVtx = 0, globalIdx = 0;
    ImVec2 clipOff = draw_data->DisplayPos;
    for (int n = 0; n < draw_data->CmdListsCount; n++) {
        const ImDrawList* cl = draw_data->CmdLists[n];
        for (int ci = 0; ci < cl->CmdBuffer.Size; ci++) {
            const ImDrawCmd* cmd = &cl->CmdBuffer[ci];
            if (cmd->UserCallback) {
                if (cmd->UserCallback == ImGui::GetPlatformIO().DrawCallback_ResetRenderState)
                    SetupRenderState(draw_data, ctx);
                else
                    cmd->UserCallback(cl, cmd);
            } else {
                ImVec2 cMin(cmd->ClipRect.x - clipOff.x, cmd->ClipRect.y - clipOff.y);
                ImVec2 cMax(cmd->ClipRect.z - clipOff.x, cmd->ClipRect.w - clipOff.y);
                if (cMax.x <= cMin.x || cMax.y <= cMin.y) continue;
                const D3D11_RECT r = {(LONG)cMin.x,(LONG)cMin.y,(LONG)cMax.x,(LONG)cMax.y};
                ctx->RSSetScissorRects(1, &r);
                ImTextureID tid = cmd->GetTexID();
                if (tid == ImTextureID_Invalid) continue; // font not uploaded yet
                auto* srv = (ID3D11ShaderResourceView*)(intptr_t)tid;
                ctx->PSSetShaderResources(0, 1, &srv);
                ctx->DrawIndexed(cmd->ElemCount, cmd->IdxOffset + globalIdx, cmd->VtxOffset + globalVtx);
            }
        }
        globalVtx += cl->VtxBuffer.Size;
        globalIdx  += cl->IdxBuffer.Size;
    }

    // Restore DX state
    ctx->RSSetScissorRects(s.ScissorCount, s.Scissors);
    ctx->RSSetViewports(s.VPCount, s.Viewports);
    ctx->RSSetState(s.RS);        if (s.RS)  s.RS->Release();
    ctx->OMSetBlendState(s.BS, s.BF, s.SM); if (s.BS)  s.BS->Release();
    ctx->OMSetDepthStencilState(s.DS, s.SR); if (s.DS) s.DS->Release();
    ctx->PSSetShaderResources(0,1,&s.PSR);   if (s.PSR) s.PSR->Release();
    ctx->PSSetSamplers(0,1,&s.PSS);          if (s.PSS) s.PSS->Release();
    ctx->PSSetShader(s.PS, s.PSI, s.PSC);   if (s.PS)  s.PS->Release();
    for (UINT i=0;i<s.PSC;i++) if(s.PSI[i]) s.PSI[i]->Release();
    ctx->VSSetShader(s.VS, s.VSI, s.VSC);   if (s.VS)  s.VS->Release();
    ctx->VSSetConstantBuffers(0,1,&s.VCB);   if (s.VCB) s.VCB->Release();
    ctx->GSSetShader(s.GS, s.GSI, s.GSC);   if (s.GS)  s.GS->Release();
    for (UINT i=0;i<s.VSC;i++) if(s.VSI[i]) s.VSI[i]->Release();
    for (UINT i=0;i<s.GSC;i++) if(s.GSI[i]) s.GSI[i]->Release();
    ctx->IASetPrimitiveTopology(s.PT);
    ctx->IASetIndexBuffer(s.IB, s.IBF, s.IBO);  if (s.IB)  s.IB->Release();
    ctx->IASetVertexBuffers(0,1,&s.VB,&s.VBS,&s.VBO); if (s.VB) s.VB->Release();
    ctx->IASetInputLayout(s.IL);             if (s.IL)  s.IL->Release();
}

// ---------------------------------------------------------------------------
// Device objects
// ---------------------------------------------------------------------------
void ImGui_ImplDX11_InvalidateDeviceObjects()
{
    ImGui_ImplDX11_Data* bd = GetBD();
    if (!bd || !bd->pd3dDevice) return;

    // Destroy all textures
    for (ImTextureData* tex : ImGui::GetPlatformIO().Textures)
        if (tex->RefCount > 0 && tex->TexID != ImTextureID_Invalid)
            DestroyTexture(tex);

    if (bd->pFontSampler)          { bd->pFontSampler->Release();          bd->pFontSampler = nullptr; }
    if (bd->pIB)                   { bd->pIB->Release();                   bd->pIB = nullptr; }
    if (bd->pVB)                   { bd->pVB->Release();                   bd->pVB = nullptr; }
    if (bd->pBlendState)           { bd->pBlendState->Release();           bd->pBlendState = nullptr; }
    if (bd->pDepthStencilState)    { bd->pDepthStencilState->Release();    bd->pDepthStencilState = nullptr; }
    if (bd->pRasterizerState)      { bd->pRasterizerState->Release();      bd->pRasterizerState = nullptr; }
    if (bd->pPixelShader)          { bd->pPixelShader->Release();          bd->pPixelShader = nullptr; }
    if (bd->pVertexConstantBuffer) { bd->pVertexConstantBuffer->Release(); bd->pVertexConstantBuffer = nullptr; }
    if (bd->pInputLayout)          { bd->pInputLayout->Release();          bd->pInputLayout = nullptr; }
    if (bd->pVertexShader)         { bd->pVertexShader->Release();         bd->pVertexShader = nullptr; }
    bd->DeviceObjectsCreated = false;
}

bool ImGui_ImplDX11_CreateDeviceObjects()
{
    ImGui_ImplDX11_Data* bd = GetBD();
    if (!bd || !bd->pd3dDevice) return false;
    if (bd->DeviceObjectsCreated) ImGui_ImplDX11_InvalidateDeviceObjects();

    static const char* vsCode =
        "cbuffer vertexBuffer : register(b0) { float4x4 ProjectionMatrix; };"
        "struct VS_INPUT  { float2 pos:POSITION; float4 col:COLOR0; float2 uv:TEXCOORD0; };"
        "struct PS_INPUT  { float4 pos:SV_POSITION; float4 col:COLOR0; float2 uv:TEXCOORD0; };"
        "PS_INPUT main(VS_INPUT input) {"
        "  PS_INPUT o; o.pos=mul(ProjectionMatrix,float4(input.pos,0,1));"
        "  o.col=input.col; o.uv=input.uv; return o; }";

    ID3DBlob* vsBlob = nullptr;
    typedef HRESULT(WINAPI* D3DCompile_t)(LPCVOID, SIZE_T, LPCSTR, const void*, const void*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);
    HMODULE hD3DC = LoadLibraryA("d3dcompiler_47.dll");
    if (!hD3DC) hD3DC = LoadLibraryA("d3dcompiler_43.dll");
    if (!hD3DC) {
        OutputDebugStringA("[ImGui DX11] d3dcompiler not found!\n");
        return false;
    }
    OutputDebugStringA("[ImGui DX11] CreateDeviceObjects: compiling shaders...\n");
    auto pD3DCompile = (D3DCompile_t)GetProcAddress(hD3DC, "D3DCompile");
    if (!pD3DCompile) return false;
    if (FAILED(pD3DCompile(vsCode, strlen(vsCode), nullptr, nullptr, nullptr,
                           "main", "vs_4_0", 0, 0, &vsBlob, nullptr))) return false;

    if (FAILED(bd->pd3dDevice->CreateVertexShader(vsBlob->GetBufferPointer(),
                                                   vsBlob->GetBufferSize(), nullptr, &bd->pVertexShader))) {
        vsBlob->Release(); return false;
    }

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,   0, (UINT)offsetof(ImDrawVert,pos), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,   0, (UINT)offsetof(ImDrawVert,uv),  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, (UINT)offsetof(ImDrawVert,col), D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    bd->pd3dDevice->CreateInputLayout(layout, 3, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &bd->pInputLayout);
    vsBlob->Release();

    D3D11_BUFFER_DESC cbd = {}; cbd.ByteWidth = sizeof(VERTEX_CB);
    cbd.Usage = D3D11_USAGE_DYNAMIC; cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    bd->pd3dDevice->CreateBuffer(&cbd, nullptr, &bd->pVertexConstantBuffer);

    static const char* psCode =
        "struct PS_INPUT { float4 pos:SV_POSITION; float4 col:COLOR0; float2 uv:TEXCOORD0; };"
        "sampler smp; Texture2D tex;"
        "float4 main(PS_INPUT i):SV_Target { return i.col*tex.Sample(smp,i.uv); }";
    ID3DBlob* psBlob = nullptr;
    if (FAILED(pD3DCompile(psCode, strlen(psCode), nullptr, nullptr, nullptr,
                           "main", "ps_4_0", 0, 0, &psBlob, nullptr))) return false;
    bd->pd3dDevice->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &bd->pPixelShader);
    psBlob->Release();

    D3D11_BLEND_DESC bld = {}; bld.RenderTarget[0] = {
        TRUE, D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_OP_ADD,
        D3D11_BLEND_ONE, D3D11_BLEND_INV_SRC_ALPHA, D3D11_BLEND_OP_ADD, D3D11_COLOR_WRITE_ENABLE_ALL };
    bd->pd3dDevice->CreateBlendState(&bld, &bd->pBlendState);

    D3D11_RASTERIZER_DESC rsd = {}; rsd.FillMode=D3D11_FILL_SOLID; rsd.CullMode=D3D11_CULL_NONE;
    rsd.ScissorEnable=TRUE; rsd.DepthClipEnable=TRUE;
    bd->pd3dDevice->CreateRasterizerState(&rsd, &bd->pRasterizerState);

    D3D11_DEPTH_STENCIL_DESC dsd = {}; dsd.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL; dsd.DepthFunc=D3D11_COMPARISON_ALWAYS;
    bd->pd3dDevice->CreateDepthStencilState(&dsd, &bd->pDepthStencilState);

    D3D11_SAMPLER_DESC sd = {}; sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_WRAP; sd.ComparisonFunc=D3D11_COMPARISON_ALWAYS;
    bd->pd3dDevice->CreateSamplerState(&sd, &bd->pFontSampler);

    bd->DeviceObjectsCreated = true;
    OutputDebugStringA("[ImGui DX11] CreateDeviceObjects: SUCCESS\n");
    return true;
}

// ---------------------------------------------------------------------------
// Init / Shutdown / NewFrame
// ---------------------------------------------------------------------------
bool ImGui_ImplDX11_Init(ID3D11Device* device, ID3D11DeviceContext* ctx)
{
    ImGuiIO& io = ImGui::GetIO();
    IM_ASSERT(io.BackendRendererUserData == nullptr);

    auto* bd = IM_NEW(ImGui_ImplDX11_Data)();
    io.BackendRendererUserData = bd;
    io.BackendRendererName     = "imgui_impl_dx11";
    // Tell ImGui 1.92 that we support the new texture management API
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

    bd->pd3dDevice        = device;  device->AddRef();
    bd->pd3dDeviceContext = ctx;     ctx->AddRef();

    return true;
}

void ImGui_ImplDX11_Shutdown()
{
    ImGui_ImplDX11_Data* bd = GetBD();
    IM_ASSERT(bd != nullptr);
    ImGuiIO& io = ImGui::GetIO();

    ImGui_ImplDX11_InvalidateDeviceObjects();
    bd->pd3dDevice->Release();
    bd->pd3dDeviceContext->Release();
    io.BackendRendererUserData = nullptr;
    io.BackendRendererName     = nullptr;
    io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures);
    IM_DELETE(bd);
}

void ImGui_ImplDX11_NewFrame()
{
    ImGui_ImplDX11_Data* bd = GetBD();
    IM_ASSERT(bd != nullptr);
    // CreateDeviceObjects is called off-thread by DX11Hook to avoid blocking
    // CS2's render thread. Don't call it here.
    if (bd->DeviceObjectsCreated)
        UpdateTextures(bd); // upload font atlas once shaders are ready
}

#endif // #ifndef IMGUI_DISABLE
