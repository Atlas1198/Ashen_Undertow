#include "RendererInternal.h"

using namespace DirectX;
using namespace RendererDetail;

bool Renderer::CreateEnvironmentTexture()
{
    // --- 1. HDR ファイルを CPU メモリに読む ---
    HdrImage image;
    if (!LoadHdr("assets/env.hdr", image) || !image.Valid())
    {
        OutputDebugStringA("assets/env.hdr の読み込みに失敗しました\n");
        return false;
    }

    // --- 2. GPU 専用メモリ(DEFAULT ヒープ)にテクスチャ本体を作る ---
    const auto defaultHeap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = image.width;
    texDesc.Height = image.height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    // HDR は 1.0 を超える値を持つので浮動小数フォーマットが必須。
    // 8bit UNORM に入れた時点で HDR ではなくなってしまう
    texDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    if (!Check(m_device->CreateCommittedResource(
        &defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,   // まず「コピー先」の状態で作る
        nullptr, IID_PPV_ARGS(&m_envTexture))))
    {
        return false;
    }

    // --- 3. 中継用の UPLOAD バッファを用意する ---
    // GPU 上のテクスチャは行ごとに 256 バイト境界へ揃える必要があり、
    // 画像本来の 1 行の長さとは一致しない。その差を吸収するため、
    // GetCopyableFootprints に「GPU が期待する配置」を教えてもらう
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    UINT   numRows = 0;
    UINT64 rowSizeBytes = 0;
    UINT64 uploadSize = 0;
    m_device->GetCopyableFootprints(&texDesc, 0, 1, 0,
        &footprint, &numRows, &rowSizeBytes, &uploadSize);

    const auto uploadHeap = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);

    auto bufDesc = BufferDescription(uploadSize);

    ComPtr<ID3D12Resource> uploadBuffer;
    if (!Check(m_device->CreateCommittedResource(
        &uploadHeap, D3D12_HEAP_FLAG_NONE, &bufDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&uploadBuffer))))
    {
        return false;
    }

    // --- 4. 中継バッファへ 1 行ずつコピー ---
    // 画像の行の長さ(rowSizeBytes)と GPU 側の行間隔(RowPitch)が
    // 違うので、まとめて memcpy はできない
    uint8_t* mapped = nullptr;
    D3D12_RANGE readRange = { 0, 0 };
    if (!Check(uploadBuffer->Map(0, &readRange, reinterpret_cast<void**>(&mapped))))
    {
        return false;
    }
    for (UINT y = 0; y < numRows; ++y)
    {
        memcpy(mapped + footprint.Offset + (SIZE_T)y * footprint.Footprint.RowPitch,
            image.pixels.data() + (size_t)y * image.width * 4,
            (size_t)rowSizeBytes);
    }
    uploadBuffer->Unmap(0, nullptr);

    // --- 5. GPU にコピーを実行させる ---
    // 初期化時なので、コマンドリストを一度回して完了を待つ
    m_commandAllocators[m_frameIndex]->Reset();
    m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), nullptr);

    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = m_envTexture.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = uploadBuffer.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = footprint;

    m_commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    // 「コピー先」→「ピクセルシェーダから読める状態」へ遷移
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_envTexture.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_commandList->ResourceBarrier(1, &barrier);

    m_commandList->Close();
    ID3D12CommandList* lists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(1, lists);

    // uploadBuffer はこの関数を抜けると破棄される。
    // GPU がコピーを終える前に消すと壊れるので、必ず完了を待つ
    WaitForGpu();

    // --- 6. シェーダから見えるヒープに SRV を作る ---
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    // 全モデルのマテリアル数に応じて必要な数が変わる。
    uint32_t materialCount = 0;
    for (const auto& model : m_models)
        materialCount += static_cast<uint32_t>(model.model->materials.size());
    if (materialCount == 0) materialCount = 1;
    heapDesc.NumDescriptors = kSrvMaterialStart + kTexturesPerMaterial * materialCount;

    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    // このフラグが「シェーダが実行中に読めるヒープ」の指定。
    // RTV/DSV ヒープには付けられない(CPU からしか使わないため)
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (!Check(m_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_srvHeap))))
    {
        return false;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = texDesc.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;
    m_device->CreateShaderResourceView(m_envTexture.Get(), &srvDesc,
        m_srvHeap->GetCPUDescriptorHandleForHeapStart());

    return true;
}

bool Renderer::CreateSkyboxPipeline()
{
    UINT compileFlags = 0;
#if defined(_DEBUG)
    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ComPtr<ID3DBlob> vs, ps, err;
    if (!Check(D3DCompileFromFile(L"shaders/Skybox.hlsl", nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "VSMain", "vs_5_0", compileFlags, 0, &vs, &err)))
    {
        if (err) OutputDebugStringA((char*)err->GetBufferPointer());
        return false;
    }
    if (!Check(D3DCompileFromFile(L"shaders/Skybox.hlsl", nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "PSMain", "ps_5_0", compileFlags, 0, &ps, &err)))
    {
        if (err) OutputDebugStringA((char*)err->GetBufferPointer());
        return false;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    // ルートシグネチャは球体用と共通(同じ b0 と t0 を使うため)
    psoDesc.pRootSignature = m_rootSignature.Get();
    psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    // 頂点バッファを使わず SV_VertexID から座標を作るので入力レイアウトは空
    psoDesc.InputLayout = { nullptr, 0 };

    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;

    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    // 背景は最初に全画面を塗るだけなので、深度テストも書き込みも不要
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    // 深度バッファはバインドされているのでフォーマットは合わせておく
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;

    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    psoDesc.SampleDesc.Count = 1;

    return Check(m_device->CreateGraphicsPipelineState(&psoDesc,
        IID_PPV_ARGS(&m_skyboxPSO)));
}

bool Renderer::CreateIrradianceMap()
{
    // 積分結果は非常に滑らかなので、この程度の解像度で十分足りる
    const UINT kWidth = 64;
    const UINT kHeight = 32;   // 等距円筒図法は横:縦 = 2:1

    // --- 1. 描き込み先のテクスチャ ---
    const auto defaultHeap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = kWidth;
    texDesc.Height = kHeight;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    // 「描画先として使う」宣言。これが無いと RTV を作れない
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    if (!Check(m_device->CreateCommittedResource(
        &defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
        IID_PPV_ARGS(&m_irradianceMap))))
    {
        return false;
    }

    // --- 2. このテクスチャ専用の RTV ---
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = 1;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (!Check(m_device->CreateDescriptorHeap(&rtvHeapDesc,
        IID_PPV_ARGS(&m_irradianceRtvHeap))))
    {
        return false;
    }
    m_device->CreateRenderTargetView(m_irradianceMap.Get(), nullptr,
        m_irradianceRtvHeap->GetCPUDescriptorHandleForHeapStart());

    // --- 3. このパス専用のルートシグネチャ ---
    // 必要なのは環境マップ 1 枚だけ。パスごとに必要最小限を宣言するのが定石
    D3D12_DESCRIPTOR_RANGE srvRange = {};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 1;
    srvRange.BaseShaderRegister = 0;   // t0
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParam = {};
    rootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParam.DescriptorTable.NumDescriptorRanges = 1;
    rootParam.DescriptorTable.pDescriptorRanges = &srvRange;
    rootParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;   // s0
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = 1;
    rootDesc.pParameters = &rootParam;
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

    ComPtr<ID3D12RootSignature> rootSig;
    if (!Check(m_device->CreateRootSignature(0, signature->GetBufferPointer(),
        signature->GetBufferSize(),
        IID_PPV_ARGS(&rootSig))))
    {
        return false;
    }

    // --- 4. PSO ---
    UINT compileFlags = 0;
#if defined(_DEBUG)
    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ComPtr<ID3DBlob> vs, ps, err;
    if (!Check(D3DCompileFromFile(L"shaders/Irradiance.hlsl", nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "VSMain", "vs_5_0", compileFlags, 0, &vs, &err)))
    {
        if (err) OutputDebugStringA((char*)err->GetBufferPointer());
        return false;
    }
    if (!Check(D3DCompileFromFile(L"shaders/Irradiance.hlsl", nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "PSMain", "ps_5_0", compileFlags, 0, &ps, &err)))
    {
        if (err) OutputDebugStringA((char*)err->GetBufferPointer());
        return false;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = rootSig.Get();
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
    // 描画先テクスチャのフォーマットと一致させること
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R32G32B32A32_FLOAT;
    psoDesc.SampleDesc.Count = 1;

    ComPtr<ID3D12PipelineState> pso;
    if (!Check(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso))))
    {
        return false;
    }

    // --- 5. 実行(起動時に 1 回だけ) ---
    m_commandAllocators[m_frameIndex]->Reset();
    m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), pso.Get());

    D3D12_CPU_DESCRIPTOR_HANDLE rtv =
        m_irradianceRtvHeap->GetCPUDescriptorHandleForHeapStart();
    // 深度バッファは不要なので nullptr
    m_commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

    // ビューポートは「描画先のサイズ」。画面サイズではない点に注意
    D3D12_VIEWPORT vp = { 0.0f, 0.0f, (float)kWidth, (float)kHeight, 0.0f, 1.0f };
    D3D12_RECT     sc = { 0, 0, (LONG)kWidth, (LONG)kHeight };
    m_commandList->RSSetViewports(1, &vp);
    m_commandList->RSSetScissorRects(1, &sc);

    m_commandList->SetGraphicsRootSignature(rootSig.Get());
    ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get() };
    m_commandList->SetDescriptorHeaps(_countof(heaps), heaps);
    // テーブルの先頭 = ヒープ 0 番 = 環境マップ
    m_commandList->SetGraphicsRootDescriptorTable(
        0, m_srvHeap->GetGPUDescriptorHandleForHeapStart());

    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->DrawInstanced(3, 1, 0, 0);

    // 「描画先」→「シェーダから読める状態」へ遷移
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_irradianceMap.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_commandList->ResourceBarrier(1, &barrier);

    m_commandList->Close();
    ID3D12CommandList* lists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(1, lists);

    // rootSig と pso はこの関数を抜けると破棄されるので、完了を待つ
    WaitForGpu();

    // --- 6. SRV をヒープの 1 番(= t1)に作る ---
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = texDesc.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;

    D3D12_CPU_DESCRIPTOR_HANDLE srvHandle =
        m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    // 1 個ぶん進めた位置が t1 に対応する
    srvHandle.ptr += m_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_device->CreateShaderResourceView(m_irradianceMap.Get(), &srvDesc, srvHandle);

    return true;
}

bool Renderer::CreatePrefilterMap()
{
    const UINT kBaseWidth = 256;
    const UINT kBaseHeight = 128;
    const UINT kMipLevels = 6;   // Triangle.hlsl の kMaxReflectionLod + 1 と一致させる

    // --- 1. ミップ付きテクスチャ ---
    const auto defaultHeap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = kBaseWidth;
    texDesc.Height = kBaseHeight;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = kMipLevels;   // irradiance との最大の違い
    texDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    if (!Check(m_device->CreateCommittedResource(
        &defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
        IID_PPV_ARGS(&m_prefilterMap))))
    {
        return false;
    }

    // --- 2. ミップごとに RTV を作る ---
    // 「テクスチャのどのミップに描くか」は RTV で指定する
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = kMipLevels;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (!Check(m_device->CreateDescriptorHeap(&rtvHeapDesc,
        IID_PPV_ARGS(&m_prefilterRtvHeap))))
    {
        return false;
    }

    const UINT rtvStep =
        m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
        m_prefilterRtvHeap->GetCPUDescriptorHandleForHeapStart();

    for (UINT mip = 0; mip < kMipLevels; ++mip)
    {
        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format = texDesc.Format;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        rtvDesc.Texture2D.MipSlice = mip;
        m_device->CreateRenderTargetView(m_prefilterMap.Get(), &rtvDesc, rtvHandle);
        rtvHandle.ptr += rtvStep;
    }

    // --- 3. ルートシグネチャ(ルート定数 + SRV テーブル) ---
    D3D12_DESCRIPTOR_RANGE srvRange = {};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 1;
    srvRange.BaseShaderRegister = 0;   // t0
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParams[2] = {};

    // ルート定数:
    // バッファもディスクリプタも作らず、値そのものをルートに埋め込む方式。
    // 「毎ドローで少しだけ変わる小さな値」に最適で、CPU 側の管理が要らない
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParams[0].Constants.ShaderRegister = 1;   // b1
    rootParams[0].Constants.RegisterSpace = 0;
    rootParams[0].Constants.Num32BitValues = 1;   // float 1 個ぶん
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[1].DescriptorTable.pDescriptorRanges = &srvRange;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
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

    ComPtr<ID3D12RootSignature> rootSig;
    if (!Check(m_device->CreateRootSignature(0, signature->GetBufferPointer(),
        signature->GetBufferSize(),
        IID_PPV_ARGS(&rootSig))))
    {
        return false;
    }

    // --- 4. PSO ---
    UINT compileFlags = 0;
#if defined(_DEBUG)
    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ComPtr<ID3DBlob> vs, ps, err;
    if (!Check(D3DCompileFromFile(L"shaders/Prefilter.hlsl", nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "VSMain", "vs_5_0", compileFlags, 0, &vs, &err)))
    {
        if (err) OutputDebugStringA((char*)err->GetBufferPointer());
        return false;
    }
    if (!Check(D3DCompileFromFile(L"shaders/Prefilter.hlsl", nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "PSMain", "ps_5_0", compileFlags, 0, &ps, &err)))
    {
        if (err) OutputDebugStringA((char*)err->GetBufferPointer());
        return false;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = rootSig.Get();
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
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R32G32B32A32_FLOAT;
    psoDesc.SampleDesc.Count = 1;

    ComPtr<ID3D12PipelineState> pso;
    if (!Check(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso))))
    {
        return false;
    }

    // --- 5. ミップごとに 1 回ずつ描画 ---
    m_commandAllocators[m_frameIndex]->Reset();
    m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), pso.Get());

    m_commandList->SetGraphicsRootSignature(rootSig.Get());
    ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get() };
    m_commandList->SetDescriptorHeaps(_countof(heaps), heaps);
    // 1 番のテーブル = 環境マップ(ヒープ先頭)
    m_commandList->SetGraphicsRootDescriptorTable(
        1, m_srvHeap->GetGPUDescriptorHandleForHeapStart());
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    rtvHandle = m_prefilterRtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT mip = 0; mip < kMipLevels; ++mip)
    {
        // ミップが 1 つ下がるごとに解像度は半分になる
        const UINT w = kBaseWidth >> mip;
        const UINT h = kBaseHeight >> mip;

        D3D12_VIEWPORT vp = { 0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f };
        D3D12_RECT     sc = { 0, 0, (LONG)w, (LONG)h };
        m_commandList->RSSetViewports(1, &vp);
        m_commandList->RSSetScissorRects(1, &sc);
        m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

        // ミップ 0 → roughness 0(鏡面)、最終ミップ → roughness 1
        const float roughness = (float)mip / (float)(kMipLevels - 1);
        m_commandList->SetGraphicsRoot32BitConstants(0, 1, &roughness, 0);

        m_commandList->DrawInstanced(3, 1, 0, 0);

        rtvHandle.ptr += rtvStep;
    }

    // 全ミップをまとめてシェーダから読める状態へ
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_prefilterMap.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_commandList->ResourceBarrier(1, &barrier);

    m_commandList->Close();
    ID3D12CommandList* lists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(1, lists);
    WaitForGpu();

    // --- 6. SRV をヒープの 2 番(= t2)に作る ---
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = texDesc.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    // 全ミップを見えるようにする。ここを 1 にするとぼけが効かない
    srvDesc.Texture2D.MipLevels = kMipLevels;

    D3D12_CPU_DESCRIPTOR_HANDLE srvHandle =
        m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    srvHandle.ptr += 2 * m_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_device->CreateShaderResourceView(m_prefilterMap.Get(), &srvDesc, srvHandle);

    return true;
}

bool Renderer::CreateBrdfLut()
{
    const UINT kSize = 256;

    // --- 1. テクスチャ ---
    const auto defaultHeap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = kSize;
    texDesc.Height = kSize;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    // 格納するのは scale と bias の 2 値だけ。しかも 0〜1 の範囲に収まるので
    // 半精度 2 成分で十分足りる(32bit RGBA の 1/4 のサイズ)
    texDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    if (!Check(m_device->CreateCommittedResource(
        &defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
        IID_PPV_ARGS(&m_brdfLut))))
    {
        return false;
    }

    // --- 2. RTV ---
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = 1;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (!Check(m_device->CreateDescriptorHeap(&rtvHeapDesc,
        IID_PPV_ARGS(&m_brdfLutRtvHeap))))
    {
        return false;
    }
    m_device->CreateRenderTargetView(m_brdfLut.Get(), nullptr,
        m_brdfLutRtvHeap->GetCPUDescriptorHandleForHeapStart());

    // --- 3. 空のルートシグネチャ ---
    // テクスチャもサンプラも定数も使わないシェーダなので、宣言する物が無い。
    // 「必要最小限を宣言する」という原則を突き詰めるとこうなる
    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = 0;
    rootDesc.pParameters = nullptr;
    rootDesc.NumStaticSamplers = 0;
    rootDesc.pStaticSamplers = nullptr;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> signature, error;
    if (!Check(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        &signature, &error)))
    {
        if (error) OutputDebugStringA((char*)error->GetBufferPointer());
        return false;
    }

    ComPtr<ID3D12RootSignature> rootSig;
    if (!Check(m_device->CreateRootSignature(0, signature->GetBufferPointer(),
        signature->GetBufferSize(),
        IID_PPV_ARGS(&rootSig))))
    {
        return false;
    }

    // --- 4. PSO ---
    UINT compileFlags = 0;
#if defined(_DEBUG)
    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ComPtr<ID3DBlob> vs, ps, err;
    if (!Check(D3DCompileFromFile(L"shaders/BrdfLut.hlsl", nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "VSMain", "vs_5_0", compileFlags, 0, &vs, &err)))
    {
        if (err) OutputDebugStringA((char*)err->GetBufferPointer());
        return false;
    }
    if (!Check(D3DCompileFromFile(L"shaders/BrdfLut.hlsl", nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "PSMain", "ps_5_0", compileFlags, 0, &ps, &err)))
    {
        if (err) OutputDebugStringA((char*)err->GetBufferPointer());
        return false;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = rootSig.Get();
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
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16_FLOAT;
    psoDesc.SampleDesc.Count = 1;

    ComPtr<ID3D12PipelineState> pso;
    if (!Check(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso))))
    {
        return false;
    }

    // --- 5. 実行 ---
    m_commandAllocators[m_frameIndex]->Reset();
    m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), pso.Get());

    D3D12_CPU_DESCRIPTOR_HANDLE rtv =
        m_brdfLutRtvHeap->GetCPUDescriptorHandleForHeapStart();
    m_commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

    D3D12_VIEWPORT vp = { 0.0f, 0.0f, (float)kSize, (float)kSize, 0.0f, 1.0f };
    D3D12_RECT     sc = { 0, 0, (LONG)kSize, (LONG)kSize };
    m_commandList->RSSetViewports(1, &vp);
    m_commandList->RSSetScissorRects(1, &sc);

    m_commandList->SetGraphicsRootSignature(rootSig.Get());
    // ディスクリプタヒープのバインドすら不要
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->DrawInstanced(3, 1, 0, 0);

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_brdfLut.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_commandList->ResourceBarrier(1, &barrier);

    m_commandList->Close();
    ID3D12CommandList* lists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(1, lists);
    WaitForGpu();

    // --- 6. SRV をヒープの 3 番(= t3)に作る ---
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = texDesc.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;

    D3D12_CPU_DESCRIPTOR_HANDLE srvHandle =
        m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    srvHandle.ptr += 3 * m_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_device->CreateShaderResourceView(m_brdfLut.Get(), &srvDesc, srvHandle);

    return true;
}
