//-------------------------------------------------
//
//          RendererPostProcess.cpp
//
//  HDR 描画先、ブルーム、トーンマップの資源と PSO を生成する。
//-------------------------------------------------

#include "RendererInternal.h"

using namespace DirectX;
using namespace RendererDetail;

// 1.0 を超える輝度を保持できる中間描画先を作る。
bool Renderer::CreateHdrBuffer()
{
    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = m_width;
    texDesc.Height = m_height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    // 16bit 浮動小数 × 4 成分。1.0 を大きく超える明るさを保持できる。
    // 32bit でも良いが、帯域が倍になる割に見た目の差はほぼ無いので
    // HDR レンダーターゲットは 16bit が業界標準
    texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    // 実際のクリア値と一致させること
    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = texDesc.Format;
    clearValue.Color[0] = 0.0f;
    clearValue.Color[1] = 0.0f;
    clearValue.Color[2] = 0.0f;
    clearValue.Color[3] = 1.0f;

    if (!Check(m_device->CreateCommittedResource(
        &defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue,
        IID_PPV_ARGS(&m_hdrBuffer))))
    {
        return false;
    }

    // --- RTV ---
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = 1;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (!Check(m_device->CreateDescriptorHeap(&rtvHeapDesc,
        IID_PPV_ARGS(&m_hdrRtvHeap))))
    {
        return false;
    }
    m_device->CreateRenderTargetView(m_hdrBuffer.Get(), nullptr,
        m_hdrRtvHeap->GetCPUDescriptorHandleForHeapStart());

    // --- SRV(ヒープ 15 番) ---
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = texDesc.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;

    D3D12_CPU_DESCRIPTOR_HANDLE handle =
        m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += kSrvHdrBuffer * m_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_device->CreateShaderResourceView(m_hdrBuffer.Get(), &srvDesc, handle);

    return true;
}

// HDR とブルームを合成し、画面表示用の色域へ変換する PSO を作る。
bool Renderer::CreateTonemapPipeline()
{
    // このパス専用のルートシグネチャ:
    //   0 番: 露出(ルート定数)
    //   1 番: HDR バッファ(テーブル)
    D3D12_DESCRIPTOR_RANGE srvRange = {};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 2;   // t0:HDR、t1:ブルーム
    srvRange.BaseShaderRegister = 0;   // t0
    srvRange.RegisterSpace = 0;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParams[2] = {};

    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParams[0].Constants.ShaderRegister = 0;   // b0
    rootParams[0].Constants.RegisterSpace = 0;
    rootParams[0].Constants.Num32BitValues = 2;   // 露出 + ブルーム強度
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[1].DescriptorTable.pDescriptorRanges = &srvRange;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // 画面と 1:1 対応なので、補間しない POINT で十分
    D3D12_STATIC_SAMPLER_DESC sampler = {};
    // ブルームは 1/4 解像度から拡大されるので LINEAR が必須。
    // HDR バッファは画面と 1:1 なので、LINEAR でも結果は変わらない
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;   // s0
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = _countof(rootParams);
    rootDesc.pParameters = rootParams;
    rootDesc.NumStaticSamplers = 1;
    rootDesc.pStaticSamplers = &sampler;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> signature, error;
    if (!Check(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        &signature, &error)))
    {
        if (error) OutputDebugStringA((char*)error->GetBufferPointer());
        return false;
    }
    if (!Check(m_device->CreateRootSignature(0, signature->GetBufferPointer(),
        signature->GetBufferSize(),
        IID_PPV_ARGS(&m_tonemapRootSignature))))
    {
        return false;
    }

    UINT compileFlags = 0;
#if defined(_DEBUG)
    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ComPtr<ID3DBlob> vs, ps, err;
    if (!Check(D3DCompileFromFile(L"shaders/Tonemap.hlsl", nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "VSMain", "vs_5_0", compileFlags, 0, &vs, &err)))
    {
        if (err) OutputDebugStringA((char*)err->GetBufferPointer());
        return false;
    }
    if (!Check(D3DCompileFromFile(L"shaders/Tonemap.hlsl", nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "PSMain", "ps_5_0", compileFlags, 0, &ps, &err)))
    {
        if (err) OutputDebugStringA((char*)err->GetBufferPointer());
        return false;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_tonemapRootSignature.Get();
    psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    psoDesc.InputLayout = { nullptr, 0 };

    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.StencilEnable = FALSE;

    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    // 最終出力はバックバッファ
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;

    return Check(m_device->CreateGraphicsPipelineState(&psoDesc,
        IID_PPV_ARGS(&m_tonemapPSO)));
}

// 輝度抽出と二方向ぼかしに使う低解像度の作業テクスチャを用意する。
bool Renderer::CreateBloomResources()
{
    m_bloomWidth = m_width / 4;
    m_bloomHeight = m_height / 4;

    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = m_bloomWidth;
    texDesc.Height = m_bloomHeight;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = texDesc.Format;
    clearValue.Color[3] = 1.0f;

    if (!Check(m_device->CreateCommittedResource(
        &defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue,
        IID_PPV_ARGS(&m_bloomA))))
    {
        return false;
    }
    if (!Check(m_device->CreateCommittedResource(
        &defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue,
        IID_PPV_ARGS(&m_bloomB))))
    {
        return false;
    }

    // --- RTV(2 枚ぶん) ---
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = 2;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (!Check(m_device->CreateDescriptorHeap(&rtvHeapDesc,
        IID_PPV_ARGS(&m_bloomRtvHeap))))
    {
        return false;
    }

    const UINT rtvStep =
        m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv =
        m_bloomRtvHeap->GetCPUDescriptorHandleForHeapStart();
    m_device->CreateRenderTargetView(m_bloomA.Get(), nullptr, rtv);
    rtv.ptr += rtvStep;
    m_device->CreateRenderTargetView(m_bloomB.Get(), nullptr, rtv);

    // --- SRV(ヒープ 16、17 番) ---
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = texDesc.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;

    const UINT srvStep = m_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE srv =
        m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    srv.ptr += kSrvBloomA * srvStep;
    m_device->CreateShaderResourceView(m_bloomA.Get(), &srvDesc, srv);
    srv.ptr += srvStep;
    m_device->CreateShaderResourceView(m_bloomB.Get(), &srvDesc, srv);

    return true;
}

bool Renderer::CreateBloomPipelines()
{
    D3D12_DESCRIPTOR_RANGE srvRange = {};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 1;
    srvRange.BaseShaderRegister = 0;   // t0
    srvRange.RegisterSpace = 0;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParams[2] = {};

    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParams[0].Constants.ShaderRegister = 0;   // b0
    rootParams[0].Constants.RegisterSpace = 0;
    rootParams[0].Constants.Num32BitValues = 4;   // float4 1 個
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[1].DescriptorTable.pDescriptorRanges = &srvRange;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // バイリニア補間を使った最適化が前提なので、必ず LINEAR にすること。
    // POINT にするとぼかしが正しくかからない
    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;   // s0
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = _countof(rootParams);
    rootDesc.pParameters = rootParams;
    rootDesc.NumStaticSamplers = 1;
    rootDesc.pStaticSamplers = &sampler;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> signature, error;
    if (!Check(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        &signature, &error)))
    {
        if (error) OutputDebugStringA((char*)error->GetBufferPointer());
        return false;
    }
    if (!Check(m_device->CreateRootSignature(0, signature->GetBufferPointer(),
        signature->GetBufferSize(),
        IID_PPV_ARGS(&m_bloomRootSignature))))
    {
        return false;
    }

    UINT compileFlags = 0;
#if defined(_DEBUG)
    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    // 2 つの PSO は「どのシェーダを使うか」以外まったく同じなので、
    // 共通処理をラムダにまとめる
    auto buildPso = [&](const wchar_t* path, ComPtr<ID3D12PipelineState>& outPso) -> bool
        {
            ComPtr<ID3DBlob> vs, ps, err;
            if (!Check(D3DCompileFromFile(path, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                "VSMain", "vs_5_0", compileFlags, 0, &vs, &err)))
            {
                if (err) OutputDebugStringA((char*)err->GetBufferPointer());
                return false;
            }
            if (!Check(D3DCompileFromFile(path, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                "PSMain", "ps_5_0", compileFlags, 0, &ps, &err)))
            {
                if (err) OutputDebugStringA((char*)err->GetBufferPointer());
                return false;
            }

            D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
            psoDesc.pRootSignature = m_bloomRootSignature.Get();
            psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
            psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
            psoDesc.InputLayout = { nullptr, 0 };

            psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
            psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
            psoDesc.RasterizerState.DepthClipEnable = TRUE;
            psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask =
                D3D12_COLOR_WRITE_ENABLE_ALL;
            psoDesc.DepthStencilState.DepthEnable = FALSE;
            psoDesc.DepthStencilState.StencilEnable = FALSE;

            psoDesc.SampleMask = UINT_MAX;
            psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            psoDesc.NumRenderTargets = 1;
            psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
            psoDesc.SampleDesc.Count = 1;

            return Check(m_device->CreateGraphicsPipelineState(&psoDesc,
                IID_PPV_ARGS(&outPso)));
        };

    if (!buildPso(L"shaders/BloomBright.hlsl", m_bloomBrightPSO)) return false;
    if (!buildPso(L"shaders/BloomBlur.hlsl", m_bloomBlurPSO))   return false;
    return true;
}
