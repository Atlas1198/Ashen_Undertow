#include "RendererInternal.h"

using namespace DirectX;
using namespace RendererDetail;

bool Renderer::CreateTrianglePipeline()
{
    // ディスクリプタテーブルを 2 つに分ける:
//   1 番: IBL(t0〜t3)。全描画で共通なので一度設定すれば済む
//   2 番: マテリアル(t4〜t8)。オブジェクトごとに切り替える
    D3D12_DESCRIPTOR_RANGE ranges[3] = {};

    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 4;   // t0〜t3
    ranges[0].BaseShaderRegister = 0;
    ranges[0].RegisterSpace = 0;
    ranges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[1].NumDescriptors = 5;   // t4〜t8
    ranges[1].BaseShaderRegister = 4;
    ranges[1].RegisterSpace = 0;
    ranges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParams[4] = {};

    // 0 番: 定数バッファ(オブジェクトごとにアドレスを変える)
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParams[0].Descriptor.ShaderRegister = 0;   // b0
    rootParams[0].Descriptor.RegisterSpace = 0;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // 1 番: IBL のテクスチャ群(共通)
    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[1].DescriptorTable.pDescriptorRanges = &ranges[0];
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // 2 番: マテリアルのテクスチャ群(オブジェクトごと)
    rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[2].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[2].DescriptorTable.pDescriptorRanges = &ranges[1];
    rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // 3 番: シャドウマップ(全描画で共通)
    ranges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[2].NumDescriptors = 1;
    ranges[2].BaseShaderRegister = 9;   // t9
    ranges[2].RegisterSpace = 0;
    ranges[2].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    rootParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[3].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[3].DescriptorTable.pDescriptorRanges = &ranges[2];
    rootParams[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // スタティックサンプラ:
    // サンプラは「どう補間するか」の設定でしかないので、ヒープを使わず
    // ルートシグネチャに直接埋め込める。使い回すサンプラはこの形が定石
    D3D12_STATIC_SAMPLER_DESC samplers[4] = {};

    // s0: 環境マップ用。経度(U)は 1 周して繋がるので WRAP、
    //     緯度(V)は上下の端で折り返すと破綻するので CLAMP
    samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].MaxLOD = D3D12_FLOAT32_MAX;
    samplers[0].ShaderRegister = 0;   // s0
    samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // s1: LUT 用。横軸は N·V なので端で折り返してはいけない
    samplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].MaxLOD = D3D12_FLOAT32_MAX;
    samplers[1].ShaderRegister = 1;   // s1
    samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // s2: モデルのテクスチャ用。UV が 1 を超えてタイリングすることがあるので
    //     両方向 WRAP にする
    samplers[2].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[2].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[2].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[2].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[2].MaxLOD = D3D12_FLOAT32_MAX;
    samplers[2].ShaderRegister = 2;   // s2
    samplers[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // s3: シャドウマップ用の比較サンプラ。
// COMPARISON フィルタを使うと、深度比較の結果をハードウェアが
// 2x2 で補間して返してくれる(これだけで既に軽い PCF になる)
    samplers[3].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    samplers[3].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    // シャドウマップの外側は「影ではない」= 白(1.0)として扱う
    samplers[3].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[3].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[3].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[3].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    samplers[3].MaxLOD = D3D12_FLOAT32_MAX;
    samplers[3].ShaderRegister = 3;   // s3
    samplers[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = _countof(rootParams);
    rootDesc.pParameters = rootParams;
    rootDesc.NumStaticSamplers = _countof(samplers);
    rootDesc.pStaticSamplers = samplers;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    // ルートシグネチャは「バイナリ列にシリアライズしてから生成する」2 段構え。
    // シェーダと同じように、実行時にコンパイルされる中間表現を経由するイメージ。
    ComPtr<ID3DBlob> signature, error;
    if (!Check(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        &signature, &error)))
    {
        // error blob にシリアライズ失敗の理由(不正なレジスタ指定など)が入る
        if (error) OutputDebugStringA((char*)error->GetBufferPointer());
        return false;
    }
    if (!Check(m_device->CreateRootSignature(0, signature->GetBufferPointer(),
        signature->GetBufferSize(),
        IID_PPV_ARGS(&m_rootSignature))))
    {
        return false;
    }

    // シェーダを実行時にファイルからコンパイルする。
    // ビルド済みバイナリ(.cso)を読む方式より起動は遅いが、
    // 後で「ホットリロード」に発展させやすい。
    UINT compileFlags = 0;
#if defined(_DEBUG)
    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ComPtr<ID3DBlob> vs, ps, compileError;
    if (!Check(D3DCompileFromFile(L"shaders/Triangle.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                  "VSMain", "vs_5_0", compileFlags, 0, &vs, &compileError)))
    {
        if (compileError) OutputDebugStringA((char*)compileError->GetBufferPointer());
        return false;
    }
    if (!Check(D3DCompileFromFile(L"shaders/Triangle.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                  "PSMain", "ps_5_0", compileFlags, 0, &ps, &compileError)))
    {
        if (compileError) OutputDebugStringA((char*)compileError->GetBufferPointer());
        return false;
    }

    // 入力レイアウト: Vertex 構造体のメモリ配置をシェーダ入力に対応づける
    D3D12_INPUT_ELEMENT_DESC inputElements[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  0,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 40,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 48,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 64,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    // PSO(パイプラインステートオブジェクト):
    // シェーダ + ラスタライザ設定 + ブレンド + 深度 + 出力フォーマット…を
    // 全部まとめて事前に焼き固める。DX11 と違い実行中に個別変更できないため、
    // 設定の組み合わせごとに PSO を作るのが DX12 流。
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_rootSignature.Get();
    psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    psoDesc.InputLayout = { inputElements, _countof(inputElements) };

    // ラスタライザ: まずはカリング無しにしておくと「頂点順序ミスで消える」事故を防げる
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;

    // ブレンド: 不透明(上書き)
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    // 深度テストを有効化。
    //   DepthWriteMask : 深度値を書き込むか。半透明描画では OFF にすることがある
    //   DepthFunc LESS : 「今の値より小さい(=手前)なら合格」
    psoDesc.DepthStencilState.DepthEnable   = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    // PSO も深度バッファのフォーマットを知っている必要がある。
    // 実際の深度バッファと食い違うと描画時にエラーになる
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;

    psoDesc.SampleMask            = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets      = 1;
    // 出力先は HDR バッファ。1.0 を超える値をそのまま書き出す
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    psoDesc.SampleDesc.Count      = 1;

    if (!Check(m_device->CreateGraphicsPipelineState(&psoDesc,
        IID_PPV_ARGS(&m_pipelineState))))
    {
        return false;
    }

    // 両面マテリアル(葉など)用に、カリングだけ切った PSO も作る。
    // PSO は不変オブジェクトなので、設定を変えるには作り直すしかない。
    // 逆に言えば「1 項目違うだけの PSO」が量産されるのが DX12 の宿命で、
    // 実務では PSO のキャッシュ管理が課題になる
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    return Check(m_device->CreateGraphicsPipelineState(&psoDesc,
        IID_PPV_ARGS(&m_pipelineStateNoCull)));
}

bool Renderer::CreateShadowMap()
{
    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = kShadowMapSize;
    texDesc.Height = kShadowMapSize;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    // TYPELESS で作るのが肝。
    // 同じメモリを「深度バッファ(D32_FLOAT)」としても
    // 「テクスチャ(R32_FLOAT)」としても解釈したいので、
    // リソース自体は型を決めずに作り、ビュー側で型を与える
    texDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = DXGI_FORMAT_D32_FLOAT;
    clearValue.DepthStencil.Depth = 1.0f;

    // 毎フレーム「テクスチャ → 深度 → テクスチャ」と往復するので、
    // テクスチャの状態で作っておくと遷移が対称になる
    if (!Check(m_device->CreateCommittedResource(
        &defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue,
        IID_PPV_ARGS(&m_shadowMap))))
    {
        return false;
    }

    // --- DSV(描き込む側のビュー) ---
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (!Check(m_device->CreateDescriptorHeap(&dsvHeapDesc,
        IID_PPV_ARGS(&m_shadowDsvHeap))))
    {
        return false;
    }

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;   // 深度としての解釈
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    m_device->CreateDepthStencilView(m_shadowMap.Get(), &dsvDesc,
        m_shadowDsvHeap->GetCPUDescriptorHandleForHeapStart());

    // --- SRV(読む側のビュー、ヒープ 14 番 = t9) ---
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;   // 数値としての解釈
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;

    D3D12_CPU_DESCRIPTOR_HANDLE handle =
        m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += kSrvShadowMap * m_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_device->CreateShaderResourceView(m_shadowMap.Get(), &srvDesc, handle);

    return true;
}

bool Renderer::CreateShadowPipeline()
{
    UINT compileFlags = 0;
#if defined(_DEBUG)
    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ComPtr<ID3DBlob> vs, ps, err;
    if (!Check(D3DCompileFromFile(L"shaders/ShadowDepth.hlsl", nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "VSMain", "vs_5_0", compileFlags, 0, &vs, &err)))
    {
        if (err) OutputDebugStringA((char*)err->GetBufferPointer());
        return false;
    }

    if (!Check(D3DCompileFromFile(L"shaders/ShadowDepth.hlsl", nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "PSMain", "ps_5_0", compileFlags, 0, &ps, &err)))
    {
        if (err) OutputDebugStringA((char*)err->GetBufferPointer());
        return false;
    }

    // 位置だけあれば深度は描ける。
    // 頂点バッファのストライドは 48 バイトのままだが、
    // 入力レイアウトで宣言しない属性は単に読まれないだけ
    D3D12_INPUT_ELEMENT_DESC inputElements[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0,
          D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
          // アルファテストのために UV も要る。offset 40 は Vertex 構造体での位置
          { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 40,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
          { "BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 48,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
          { "BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 64,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_rootSignature.Get();
    psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    // アルファテストのために PS が必要になった。
    // 色は書かないので、深度専用であることに変わりはない
    psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    psoDesc.InputLayout = { inputElements, _countof(inputElements) };

    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    // 両面マテリアルの影も落としたいのでカリングしない
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;

    // 深度バイアス:
    // 同じ面を「深度を記録するとき」と「比較するとき」で計算すると、
    // 浮動小数の丸め誤差で自分自身を遮ってしまい、縞模様が出る
    // (シャドウアクネ)。記録する深度を少しだけ奥にずらして回避する。
    // SlopeScaled は「面が光に対して傾いているほど誤差が大きい」性質への補正
    psoDesc.RasterizerState.DepthBias = 2000;
    psoDesc.RasterizerState.SlopeScaledDepthBias = 2.0f;
    psoDesc.RasterizerState.DepthBiasClamp = 0.0f;

    psoDesc.DepthStencilState.DepthEnable = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    psoDesc.DepthStencilState.StencilEnable = FALSE;

    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 0;   // カラー出力なし
    psoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    psoDesc.SampleDesc.Count = 1;

    return Check(m_device->CreateGraphicsPipelineState(&psoDesc,
        IID_PPV_ARGS(&m_shadowPSO)));
}
