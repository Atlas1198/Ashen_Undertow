//----------------------------------------------------------------------------
// Renderer.cpp
// DX12 初期化 → 三角形を 1 枚描くまで。
//
// 毎フレームの流れ(Render):
//   1. アロケータ/コマンドリストを Reset
//   2. バックバッファを PRESENT → RENDER_TARGET へバリア遷移
//   3. クリアして三角形を描く
//   4. RENDER_TARGET → PRESENT へ戻すバリア
//   5. コマンドリストを閉じてキューに投げる
//   6. Present して、フェンスで同期
//----------------------------------------------------------------------------
#include "Renderer.h"
#include "ModelManager.h"

#include <map>
#include <utility>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <cassert>
#include <vector>
#include <cmath>

using namespace DirectX;

namespace
{
    // 頂点フォーマット。
// GltfVertex とメモリ配置を完全に一致させてあるので、
// ローダーの出力をそのまま memcpy できる
    struct Vertex
    {
        XMFLOAT3 position;   // offset  0
        XMFLOAT3 normal;     // offset 12
        XMFLOAT4 tangent;    // offset 24 : xyz = 接線、w = 従法線の向き
        XMFLOAT2 uv;         // offset 40
    };                       // 合計 48 バイト

    static_assert(sizeof(Vertex) == sizeof(GltfVertex),
        "Vertex と GltfVertex のメモリ配置が一致していません");

    // 定数バッファの中身。HLSL の cbuffer SceneConstants と
   // メンバの並び・型を一致させること。
    struct SceneConstants
    {
        XMFLOAT4X4 mvp;
        XMFLOAT4X4 world;
		XMFLOAT4X4 invViewProj; // 視点位置の計算に必要なので、ViewProj の逆行列も渡す

        XMFLOAT3 lightDirection;
		float padding0; // HLSL の float3 は 16byte 境界に揃うので、CPU 側も揃える
        XMFLOAT3 lightColor;
        float padding1;
        XMFLOAT3 ambientColor;
		float padding2;

        // --- 視点(鏡面反射の計算に必要) ---
        XMFLOAT3 cameraPosition;
		float padding3;

        // --- マテリアル ---
		XMFLOAT3 albedo;    //非金属では拡散色、金属では反射色
		float metallic;     //0 = 非金属、1 = 金属
		float roughness;    //0 = 鏡面、1 = ざらざら
		float padding4; // HLSL の float3 は 16byte 境界に揃うので、CPU 側も揃える
		float padding5; // HLSL の float3 は 16byte 境界に揃うので、CPU 側も揃える
		float padding6; // HLSL の float3 は 16byte 境界に揃うので、CPU 側も揃える
		XMFLOAT3 emissiveFactor;   // 自己発光色
		float padding7; // HLSL の float3 は 16byte 境界に揃うので、CPU 側も揃える
        XMFLOAT4X4 lightViewProj;   // 64 : ライト視点のビュー射影行列
        float    alphaCutoff;        //  4 : 負なら常に通る
        float    padding8;
        float    padding9;
        float    padding10;
    };

    static_assert(sizeof(SceneConstants) % 16 == 0,
        "SceneConstants は 16 バイト境界に揃っていません");

    // HRESULT のチェックを簡潔に書くためのヘルパ
    inline bool Check(HRESULT hr) { return SUCCEEDED(hr); }

    // sRGB エンコードされた値 ↔ リニア値
 //
 // 入力が uint8_t なら取りうる値は 256 通りしかない。
 // 毎回 powf を呼ぶ代わりに、初回に一度だけ表を作って引く。
 // powf は数十サイクルかかるので、ミップ生成のような
 // 数億回のループでは効き方が桁違いになる
    inline const float* SrgbToLinearTable()
    {
        static float table[256];
        static bool  initialized = false;
        if (!initialized)
        {
            for (int i = 0; i < 256; ++i)
            {
                const float c = i / 255.0f;
                table[i] = (c <= 0.04045f) ? c / 12.92f
                    : powf((c + 0.055f) / 1.055f, 2.4f);
            }
            initialized = true;
        }
        return table;
    }

    // 逆方向は入力が連続値だが、出力は 8bit に丸められる。
// ならば入力を量子化して引いても誤差は見えない。
//
// 添字に sqrt を使うのは、リニア値を等間隔で刻むと
// 暗部の精度が落ちるため。sqrt をかませると人間の知覚に近い
// 分布になり、少ない要素数でも破綻しない。
// sqrtf は CPU の命令 1 つで済むので powf より桁違いに軽い
    inline uint8_t LinearToSrgbByte(float c)
    {
        static uint8_t table[4096];
        static bool    initialized = false;
        if (!initialized)
        {
            for (int i = 0; i < 4096; ++i)
            {
                const float t = i / 4095.0f;
                const float lin = t * t;             // sqrt で引くので 2 乗して戻す
                const float s = (lin <= 0.0031308f)
                    ? lin * 12.92f
                    : 1.055f * powf(lin, 1.0f / 2.4f) - 0.055f;
                table[i] = (uint8_t)(s * 255.0f + 0.5f);
            }
            initialized = true;
        }

        if (c <= 0.0f) return 0;
        if (c >= 1.0f) return 255;
        return table[(int)(sqrtf(c) * 4095.0f + 0.5f)];
    }
}

Renderer::~Renderer()
{
    // ComPtr が解放される前に、GPU がリソースを使い終わるのを待つ。
    // これを忘れると「使用中のリソースを破棄した」ことになり得る。
    if (m_commandQueue && m_fence)
    {
        WaitForGpu();
    }
    if (m_fenceEvent)
    {
        CloseHandle(m_fenceEvent);
    }
}

bool Renderer::Initialize(HWND hwnd, uint32_t width, uint32_t height,
    ModelManager& modelManager)
{
    m_width  = width;
    m_height = height;

    m_modelHandle = modelManager.Load("assets/model.glb");
    m_model = modelManager.GetModel(m_modelHandle);
    if (!m_model) return false;

    // 高精度タイマーの初期化。
    // 周波数は起動中に変化しないので一度だけ取得すればよい
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    m_perfFrequency = (uint64_t)freq.QuadPart;
    m_lastCounter = (uint64_t)counter.QuadPart;

    if (!CreateDevice())            return false;
    if (!CreateCommandObjects())    return false;
    if (!CreateSwapChain(hwnd))     return false;
    if (!CreateRenderTargets())     return false;
    if (!CreateFence())             return false;
    if (!CreateTrianglePipeline())  return false;
    if (!CreateVertexBuffer())      return false;
    if (!CreateConstantBuffer())     return false;
	if (!CreateDepthBuffer())        return false;
    if (!CreateEnvironmentTexture()) return false;
    if (!CreateSkyboxPipeline())     return false;
    if (!CreateIrradianceMap())      return false;
    if (!CreatePrefilterMap())       return false;
    if (!CreateBrdfLut())            return false;
    if (!CreateMaterialTextures())  return false;
	if (!CreateFloor())             return false;
    if (!CreateShadowMap())         return false;
    if (!CreateShadowPipeline())    return false;
    if (!CreateHdrBuffer())         return false;
    if (!CreateTonemapPipeline())   return false;
    if (!CreateBloomResources())    return false;
    if (!CreateBloomPipelines())    return false;
    return true;
}

//----------------------------------------------------------------------------
// デバイス作成
//----------------------------------------------------------------------------
bool Renderer::CreateDevice()
{
    UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
    // デバッグレイヤー:
    // API の誤用(バリア忘れ、ステート不一致など)を出力ウィンドウに報告してくれる。
    // DX12 学習の生命線なので Debug ビルドでは必ず有効にする。
    {
        ComPtr<ID3D12Debug> debugController;
        if (Check(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
        {
            debugController->EnableDebugLayer();
            dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
    }
#endif

    if (!Check(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&m_factory))))
    {
        return false;
    }

    // アダプタ(=物理GPU)を性能順に列挙して、最初に D3D12 デバイスを
    // 作れたものを採用する。ノートPCの内蔵GPU/外部GPU問題への対策。
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0;
         m_factory->EnumAdapterByGpuPreference(
             i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
             IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND;
         ++i)
    {
        DXGI_ADAPTER_DESC1 desc;
        adapter->GetDesc1(&desc);

        // ソフトウェアエミュレータ(WARP)はスキップ
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;

        if (Check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0,
                                    IID_PPV_ARGS(&m_device))))
        {
#if defined(_DEBUG)
            // エラー/警告が出た瞬間にデバッガを止める。
            // 呼び出しスタックがそのまま残るので、原因箇所が一発で分かる。
            ComPtr<ID3D12InfoQueue> infoQueue;
            if (SUCCEEDED(m_device->QueryInterface(IID_PPV_ARGS(&infoQueue))))
            {
                infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
                infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
            }
#endif
            return true;
        }
    }
    return false;
}

//----------------------------------------------------------------------------
// コマンドキュー / アロケータ / リスト
//----------------------------------------------------------------------------
bool Renderer::CreateCommandObjects()
{
    // DIRECT キュー = 描画もコピーも計算も全部できる汎用キュー
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type  = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    if (!Check(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue))))
    {
        return false;
    }

    // アロケータはフレーム数分。
    // 「GPU が読んでいる最中の記録メモリを CPU が上書きしない」ため。
    for (uint32_t i = 0; i < kFrameCount; ++i)
    {
        if (!Check(m_device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(&m_commandAllocators[i]))))
        {
            return false;
        }
    }

    // コマンドリスト自体は 1 本でよい(Reset 時にアロケータを差し替える)。
    // 作成直後は「記録中」状態なので、一旦 Close して閉じておく。
    if (!Check(m_device->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT,
            m_commandAllocators[0].Get(), nullptr,
            IID_PPV_ARGS(&m_commandList))))
    {
        return false;
    }
    m_commandList->Close();
    return true;
}

//----------------------------------------------------------------------------
// スワップチェーン
//----------------------------------------------------------------------------
bool Renderer::CreateSwapChain(HWND hwnd)
{
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.BufferCount = kFrameCount;
    desc.Width       = m_width;
    desc.Height      = m_height;
    // バックバッファは 8bit sRGB 相当。HDR パイプラインを作るときは
    // 中間バッファを R16G16B16A16_FLOAT にして最後にトーンマップしてここに書く。
    desc.Format      = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    // FLIP_DISCARD: 現行の推奨モード。Present したバッファを OS が直接使う。
    desc.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.SampleDesc.Count = 1;   // MSAA なし

    ComPtr<IDXGISwapChain1> swapChain1;
    if (!Check(m_factory->CreateSwapChainForHwnd(
            m_commandQueue.Get(),   // スワップチェーンはキューに紐づく(DX12の特徴)
            hwnd, &desc, nullptr, nullptr, &swapChain1)))
    {
        return false;
    }

    // Alt+Enter の自動フルスクリーン切替は無効化(自前で制御する方が事故らない)
    m_factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    if (!Check(swapChain1.As(&m_swapChain))) return false;
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
    return true;
}

//----------------------------------------------------------------------------
// レンダーターゲットビュー(RTV)
//----------------------------------------------------------------------------
bool Renderer::CreateRenderTargets()
{
    // ディスクリプタヒープ = 「リソースの説明書(ビュー)」を並べる配列。
    // RTV 用ヒープに、バックバッファ 2 枚分の RTV を作って並べる。
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = kFrameCount;
    heapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (!Check(m_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_rtvHeap))))
    {
        return false;
    }

    // ディスクリプタのサイズは GPU ごとに違うので API から取得する
    m_rtvDescriptorSize =
        m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
        m_rtvHeap->GetCPUDescriptorHandleForHeapStart();

    for (uint32_t i = 0; i < kFrameCount; ++i)
    {
        // スワップチェーンからバックバッファ本体を取得し、RTV を作る
        if (!Check(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_renderTargets[i]))))
        {
            return false;
        }
        m_device->CreateRenderTargetView(m_renderTargets[i].Get(), nullptr, rtvHandle);
        rtvHandle.ptr += m_rtvDescriptorSize;   // 次のスロットへ
    }
    return true;
}

//----------------------------------------------------------------------------
// フェンス(CPU/GPU 同期)
//----------------------------------------------------------------------------
bool Renderer::CreateFence()
{
    // フェンス = GPU が到達したら値を書き込むカウンタ。
    // 「キューに Signal(fence, N) を積む → GPU がそこまで実行したら fence が N になる」
    // CPU 側は fence の値を見れば GPU の進捗が分かる。
    if (!Check(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence))))
    {
        return false;
    }
    m_fenceValues[m_frameIndex] = 1;

    // フェンスが目標値に達したときに起こしてもらうためのイベント
    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    return m_fenceEvent != nullptr;
}

//----------------------------------------------------------------------------
// ルートシグネチャ + シェーダ + PSO
//----------------------------------------------------------------------------
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

//----------------------------------------------------------------------------
// 頂点バッファ
//----------------------------------------------------------------------------
bool Renderer::CreateVertexBuffer()
{
    if (!m_model || !m_model->Valid())
    {
        OutputDebugStringA("assets/model.glb の読み込みに失敗しました\n");
        return false;
    }

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    D3D12_RANGE readRange = { 0, 0 };

    m_meshes.reserve(m_model->primitives.size());

    for (const GltfPrimitive& prim : m_model->primitives)
    {
        if (prim.vertices.empty() || prim.indices.empty()) continue;

        MeshPrimitive mesh;
        mesh.material = prim.material;
        mesh.indexCount = (UINT)prim.indices.size();

        // マテリアルからアルファと両面の設定を取り出しておく。
        // 描画時にマテリアル配列を引き直さずに済む
        if (prim.material >= 0 && (size_t)prim.material < m_model->materials.size())
        {
            const GltfMaterial& mat = m_model->materials[(size_t)prim.material];
            // MASK 以外は「常に通る」ことを表す負の値にする。
            // こうするとシェーダ側に分岐が要らない
            mesh.alphaCutoff = (mat.alphaMode == GltfMaterial::AlphaMode::Mask)
                ? mat.alphaCutoff : -1.0f;
            mesh.doubleSided = mat.doubleSided;
        }

        const UINT vertexBufferSize = (UINT)(prim.vertices.size() * sizeof(GltfVertex));
        const UINT indexBufferSize = (UINT)(prim.indices.size() * sizeof(uint32_t));

        // --- 頂点バッファ ---
        resourceDesc.Width = vertexBufferSize;
        if (!Check(m_device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &resourceDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&mesh.vertexBuffer))))
        {
            return false;
        }

        void* mapped = nullptr;
        if (!Check(mesh.vertexBuffer->Map(0, &readRange, &mapped))) return false;
        memcpy(mapped, prim.vertices.data(), vertexBufferSize);
        mesh.vertexBuffer->Unmap(0, nullptr);

        mesh.vertexBufferView.BufferLocation = mesh.vertexBuffer->GetGPUVirtualAddress();
        mesh.vertexBufferView.StrideInBytes = sizeof(Vertex);
        mesh.vertexBufferView.SizeInBytes = vertexBufferSize;

        // --- インデックスバッファ ---
        resourceDesc.Width = indexBufferSize;
        if (!Check(m_device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &resourceDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&mesh.indexBuffer))))
        {
            return false;
        }

        void* mappedIndex = nullptr;
        if (!Check(mesh.indexBuffer->Map(0, &readRange, &mappedIndex))) return false;
        memcpy(mappedIndex, prim.indices.data(), indexBufferSize);
        mesh.indexBuffer->Unmap(0, nullptr);

        mesh.indexBufferView.BufferLocation = mesh.indexBuffer->GetGPUVirtualAddress();
        mesh.indexBufferView.SizeInBytes = indexBufferSize;
        mesh.indexBufferView.Format = DXGI_FORMAT_R32_UINT;

        m_meshes.push_back(std::move(mesh));
    }

    // ※ 本来この規模のデータは DEFAULT ヒープへコピーすべき。
    //    UPLOAD ヒープのままだと GPU からの読み出しが遅い。
    //    正しさより手順の単純さを優先している箇所
    return !m_meshes.empty();
}

//----------------------------------------------------------------------------
// 深度バッファ(Zバッファ)
//----------------------------------------------------------------------------
bool Renderer::CreateDepthBuffer()
{
    // DSV 用のディスクリプタヒープ。深度バッファは 1 枚なので 1 個でよい
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = 1;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (!Check(m_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_dsvHeap))))
    {
        return false;
    }

    // DEFAULT ヒープ = GPU 専用メモリ。CPU からは触れないが GPU からは最速。
    // 深度バッファは GPU が読み書きするだけなので DEFAULT が正しい選択。
    // (毎フレーム CPU が書き込む定数バッファの UPLOAD と対比して覚えるとよい)
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC resourceDesc = {};
    // 今までのバッファと違い、これは 2D テクスチャ(画面と同じ解像度)
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Width = m_width;
    resourceDesc.Height = m_height;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    // D32_FLOAT = 32bit 浮動小数の深度のみ。
    // ステンシルも使いたい場合は D24_UNORM_S8_UINT を選ぶ
    resourceDesc.Format = DXGI_FORMAT_D32_FLOAT;
    resourceDesc.SampleDesc.Count = 1;
    // テクスチャのメモリ配置は GPU に最適な形を選ばせる(バッファは ROW_MAJOR だった)
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    // このフラグが無いと深度バッファとして使えない
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    // 最適化されたクリア値:
    // 「このリソースは 1.0 でクリアされる」と事前に宣言すると、
    // GPU が高速クリアの経路を使えるようになる。
    // ここの値と実際の ClearDepthStencilView の値が食い違うと
    // デバッグレイヤーが警告を出すので、必ず揃えること
    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = DXGI_FORMAT_D32_FLOAT;
    clearValue.DepthStencil.Depth = 1.0f;
    clearValue.DepthStencil.Stencil = 0;

    if (!Check(m_device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &resourceDesc,
        // 最初から「深度書き込み中」の状態で作る。
        // 毎フレーム同じ用途にしか使わないので、バリアでの遷移が要らない
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &clearValue,
        IID_PPV_ARGS(&m_depthBuffer))))
    {
        return false;
    }

    // DSV(深度ステンシルビュー)を作る。
    // RTV と同じく「このリソースをこう解釈して使う」という説明書
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    m_device->CreateDepthStencilView(m_depthBuffer.Get(), &dsvDesc,
        m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
    return true;
}

//----------------------------------------------------------------------------
// 環境マップ(HDRI)の読み込みと GPU への転送
//----------------------------------------------------------------------------
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
    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

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

    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC bufDesc = {};
    bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufDesc.Width = uploadSize;
    bufDesc.Height = 1;
    bufDesc.DepthOrArraySize = 1;
    bufDesc.MipLevels = 1;
    bufDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufDesc.SampleDesc.Count = 1;
    bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

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
    // マテリアル数に応じて必要な数が変わる。
    // モデルが未読み込みでも 1 個ぶんは確保しておく
    const uint32_t materialCount =
        m_model->materials.empty() ? 1u : (uint32_t)m_model->materials.size();
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

//----------------------------------------------------------------------------
// スカイボックス用 PSO
//----------------------------------------------------------------------------
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

//----------------------------------------------------------------------------
// irradiance map の生成(環境マップの半球積分)
//
// 初めての「画面以外への描画」。
// テクスチャを作り、そこへ全画面三角形を 1 回描いて結果を焼き付ける
//----------------------------------------------------------------------------
bool Renderer::CreateIrradianceMap()
{
    // 積分結果は非常に滑らかなので、この程度の解像度で十分足りる
    const UINT kWidth = 64;
    const UINT kHeight = 32;   // 等距円筒図法は横:縦 = 2:1

    // --- 1. 描き込み先のテクスチャ ---
    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

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

//----------------------------------------------------------------------------
// prefilter map の生成(roughness 別にぼかした環境マップ)
//
// irradiance との違いは 2 つ:
//   ・ミップを持ち、ミップごとに別の roughness で描く
//   ・roughness をルート定数でシェーダに渡す
//----------------------------------------------------------------------------
bool Renderer::CreatePrefilterMap()
{
    const UINT kBaseWidth = 256;
    const UINT kBaseHeight = 128;
    const UINT kMipLevels = 6;   // Triangle.hlsl の kMaxReflectionLod + 1 と一致させる

    // --- 1. ミップ付きテクスチャ ---
    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

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

//----------------------------------------------------------------------------
// BRDF LUT の生成
//
// これまでの 2 つと違い、環境マップを一切参照しない。
// そのためルートシグネチャは「空」でよい
//----------------------------------------------------------------------------
bool Renderer::CreateBrdfLut()
{
    const UINT kSize = 256;

    // --- 1. テクスチャ ---
    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

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

//----------------------------------------------------------------------------
// テクスチャ 1 枚を GPU へ転送する共通処理
//
// CreateEnvironmentTexture でやったことと同じ手順を、
// 使い回せる形に切り出したもの
//----------------------------------------------------------------------------
bool Renderer::UploadTexture(const void* pixels, UINT width, UINT height,
    DXGI_FORMAT format, ComPtr<ID3D12Resource>& outResource)
{
    // --- 1. ミップレベル数を決める ---
 // 長辺が 1 になるまで半分にできる回数 + 1
    UINT mipCount = 1;
    for (UINT size = (width > height ? width : height); size > 1; size >>= 1)
        ++mipCount;

    // baseColor や emissive は sRGB 格納。縮小時の平均を
    // リニア空間で行う必要があるので、フォーマットから判定する
    const bool isSrgb = (format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);

    // --- 2. CPU 側でミップチェーンを作る ---
    // levels[0] が原寸、以降 1/2 ずつ
    std::vector<std::vector<uint8_t>> levels(mipCount);
    std::vector<UINT> levelWidth(mipCount), levelHeight(mipCount);

    levelWidth[0] = width;
    levelHeight[0] = height;
    levels[0].assign((const uint8_t*)pixels,
        (const uint8_t*)pixels + (size_t)width * height * 4);

    for (UINT mip = 1; mip < mipCount; ++mip)
    {
        const UINT srcW = levelWidth[mip - 1];
        const UINT srcH = levelHeight[mip - 1];
        const UINT dstW = (srcW > 1) ? srcW / 2 : 1;
        const UINT dstH = (srcH > 1) ? srcH / 2 : 1;

        levelWidth[mip] = dstW;
        levelHeight[mip] = dstH;
        levels[mip].resize((size_t)dstW * dstH * 4);

        const std::vector<uint8_t>& src = levels[mip - 1];
        std::vector<uint8_t>& dst = levels[mip];

        for (UINT y = 0; y < dstH; ++y)
        {
            // 元が 1 ピクセルしかない軸は、同じ行/列を 2 回読む
            const UINT y0 = (srcH > 1) ? y * 2 : 0;
            const UINT y1 = (srcH > 1) ? y * 2 + 1 : 0;

            for (UINT x = 0; x < dstW; ++x)
            {
                const UINT x0 = (srcW > 1) ? x * 2 : 0;
                const UINT x1 = (srcW > 1) ? x * 2 + 1 : 0;

                const size_t s00 = ((size_t)y0 * srcW + x0) * 4;
                const size_t s01 = ((size_t)y0 * srcW + x1) * 4;
                const size_t s10 = ((size_t)y1 * srcW + x0) * 4;
                const size_t s11 = ((size_t)y1 * srcW + x1) * 4;
                const size_t d = ((size_t)y * dstW + x) * 4;

                for (int c = 0; c < 4; ++c)
                {
                    // アルファ(c == 3)は常にリニア扱い
                    const bool convert = isSrgb && (c < 3);
                    const float* toLinear = SrgbToLinearTable();

                    float v0, v1, v2, v3;
                    if (convert)
                    {
                        // 表を引くだけ。powf は呼ばれない
                        v0 = toLinear[src[s00 + c]];
                        v1 = toLinear[src[s01 + c]];
                        v2 = toLinear[src[s10 + c]];
                        v3 = toLinear[src[s11 + c]];
                    }
                    else
                    {
                        v0 = src[s00 + c] / 255.0f;
                        v1 = src[s01 + c] / 255.0f;
                        v2 = src[s10 + c] / 255.0f;
                        v3 = src[s11 + c] / 255.0f;
                    }

                    const float avg = (v0 + v1 + v2 + v3) * 0.25f;
                    dst[d + c] = convert ? LinearToSrgbByte(avg)
                        : (uint8_t)(avg * 255.0f + 0.5f);
                }
            }
        }
    }

    // --- 3. テクスチャ本体(ミップ付き) ---
    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = (UINT16)mipCount;
    texDesc.Format = format;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    if (!Check(m_device->CreateCommittedResource(
        &defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(&outResource))))
    {
        return false;
    }

    // --- 4. 全ミップぶんの配置を一括で問い合わせる ---
    // 第 3 引数がサブリソース数。ミップ 1 枚ごとに 1 サブリソース
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(mipCount);
    std::vector<UINT>   numRows(mipCount);
    std::vector<UINT64> rowSizes(mipCount);
    UINT64 uploadSize = 0;
    m_device->GetCopyableFootprints(&texDesc, 0, mipCount, 0,
        footprints.data(), numRows.data(),
        rowSizes.data(), &uploadSize);

    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC bufDesc = {};
    bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufDesc.Width = uploadSize;
    bufDesc.Height = 1;
    bufDesc.DepthOrArraySize = 1;
    bufDesc.MipLevels = 1;
    bufDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufDesc.SampleDesc.Count = 1;
    bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> uploadBuffer;
    if (!Check(m_device->CreateCommittedResource(
        &uploadHeap, D3D12_HEAP_FLAG_NONE, &bufDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&uploadBuffer))))
    {
        return false;
    }

    // --- 5. 中継バッファへ全ミップを書き込む ---
    uint8_t* mapped = nullptr;
    D3D12_RANGE readRange = { 0, 0 };
    if (!Check(uploadBuffer->Map(0, &readRange, reinterpret_cast<void**>(&mapped))))
    {
        return false;
    }
    for (UINT mip = 0; mip < mipCount; ++mip)
    {
        for (UINT y = 0; y < numRows[mip]; ++y)
        {
            memcpy(mapped + footprints[mip].Offset
                + (SIZE_T)y * footprints[mip].Footprint.RowPitch,
                levels[mip].data() + (size_t)y * levelWidth[mip] * 4,
                (size_t)rowSizes[mip]);
        }
    }
    uploadBuffer->Unmap(0, nullptr);

    // --- 6. ミップごとにコピーコマンドを積む ---
    m_commandAllocators[m_frameIndex]->Reset();
    m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), nullptr);

    for (UINT mip = 0; mip < mipCount; ++mip)
    {
        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource = outResource.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = mip;   // ミップ番号 = サブリソース番号

        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.pResource = uploadBuffer.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint = footprints[mip];

        m_commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    }

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = outResource.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_commandList->ResourceBarrier(1, &barrier);

    m_commandList->Close();
    ID3D12CommandList* lists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(1, lists);
    WaitForGpu();

    return true;
}

//----------------------------------------------------------------------------
// モデルのマテリアルテクスチャ
//----------------------------------------------------------------------------
bool Renderer::CreateMaterialTextures()
{
    const uint8_t whitePixel[4] = { 255, 255, 255, 255 };
    const uint8_t flatNormalPixel[4] = { 128, 128, 255, 255 };

    const UINT step = m_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    const D3D12_CPU_DESCRIPTOR_HANDLE heapStart =
        m_srvHeap->GetCPUDescriptorHandleForHeapStart();

    // 画像添字から実体を引く。無効なら nullptr
    auto findImage = [&](int index) -> const GltfImage*
        {
            if (index < 0 || (size_t)index >= m_model->images.size()) return nullptr;
            const GltfImage& img = m_model->images[(size_t)index];
            return img.Valid() ? &img : nullptr;
        };

    auto createSrv = [&](ID3D12Resource* resource, DXGI_FORMAT format, UINT slot)
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format = format;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            // -1 は「このリソースが持つ全ミップを使う」の意味。
            // ここを 1 のままにするとミップを作っても使われない
            srvDesc.Texture2D.MipLevels = (UINT)-1;

            D3D12_CPU_DESCRIPTOR_HANDLE handle = heapStart;
            handle.ptr += (SIZE_T)slot * step;
            m_device->CreateShaderResourceView(resource, &srvDesc, handle);
        };

    // 同じ画像を複数のマテリアルが参照することがある。
    // 一度アップロードしたものは使い回して、VRAM と起動時間を節約する
    std::map<std::pair<int, DXGI_FORMAT>, ComPtr<ID3D12Resource>> cache;

    // テクスチャ 1 枚を用意して、指定スロットに SRV を作る
    auto loadInto = [&](int imageIndex, DXGI_FORMAT format,
        const uint8_t* fallback, UINT slot) -> bool
        {
            const GltfImage* img = findImage(imageIndex);

            // テクスチャが無い場合は 1x1 の無害な値。キャッシュ対象外
            if (img == nullptr)
            {
                ComPtr<ID3D12Resource> tex;
                if (!UploadTexture(fallback, 1, 1, format, tex)) return false;
                createSrv(tex.Get(), format, slot);
                m_materialTextures.push_back(tex);
                return true;
            }

            const auto key = std::make_pair(imageIndex, format);
            auto it = cache.find(key);
            if (it == cache.end())
            {
                ComPtr<ID3D12Resource> tex;
                if (!UploadTexture(img->pixels.data(), img->width, img->height, format, tex))
                    return false;
                m_materialTextures.push_back(tex);
                it = cache.emplace(key, tex).first;
            }
            createSrv(it->second.Get(), format, slot);
            return true;
        };

    // --- 各マテリアルのテクスチャ 5 枚を、5 個ずつ並べていく ---
    for (size_t i = 0; i < m_model->materials.size(); ++i)
    {
        const GltfMaterial& mat = m_model->materials[i];
        const UINT base = kSrvMaterialStart + (UINT)i * kTexturesPerMaterial;

        // 色として見えるものは sRGB、数値として使うものはリニア
        if (!loadInto(mat.baseColorImage, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
            whitePixel, base + 0)) return false;
        if (!loadInto(mat.metallicRoughnessImage, DXGI_FORMAT_R8G8B8A8_UNORM,
            whitePixel, base + 1)) return false;
        if (!loadInto(mat.normalImage, DXGI_FORMAT_R8G8B8A8_UNORM,
            flatNormalPixel, base + 2)) return false;
        if (!loadInto(mat.occlusionImage, DXGI_FORMAT_R8G8B8A8_UNORM,
            whitePixel, base + 3)) return false;
        if (!loadInto(mat.emissiveImage, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
            whitePixel, base + 4)) return false;
    }

    // --- 既定マテリアル(床や、マテリアルを持たないプリミティブ用) ---
    if (!UploadTexture(whitePixel, 1, 1,
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, m_defaultWhiteSrgb)) return false;
    if (!UploadTexture(whitePixel, 1, 1,
        DXGI_FORMAT_R8G8B8A8_UNORM, m_defaultWhite))     return false;
    if (!UploadTexture(flatNormalPixel, 1, 1,
        DXGI_FORMAT_R8G8B8A8_UNORM, m_defaultNormal))    return false;

    createSrv(m_defaultWhiteSrgb.Get(), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, kSrvDefaultMaterial + 0);
    createSrv(m_defaultWhite.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, kSrvDefaultMaterial + 1);
    createSrv(m_defaultNormal.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, kSrvDefaultMaterial + 2);
    createSrv(m_defaultWhite.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, kSrvDefaultMaterial + 3);
    createSrv(m_defaultWhiteSrgb.Get(), DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, kSrvDefaultMaterial + 4);

    return true;
}

//----------------------------------------------------------------------------
// 床(影を受けるための平面)
//
// ワールド空間で直接作るので、ワールド行列は単位行列のままでよい
//----------------------------------------------------------------------------
bool Renderer::CreateFloor()
{
    const float kHalfSize = 12.0f;   // 一辺の半分
    const float kY = -1.05f;  // ヘルメットの底あたり

    // 法線は真上、接線は +X 方向
    const Vertex vertices[] =
    {
        { { -kHalfSize, kY, -kHalfSize }, { 0, 1, 0 }, { 1, 0, 0, 1 }, { 0.0f, 0.0f } },
        { { -kHalfSize, kY,  kHalfSize }, { 0, 1, 0 }, { 1, 0, 0, 1 }, { 0.0f, 1.0f } },
        { {  kHalfSize, kY,  kHalfSize }, { 0, 1, 0 }, { 1, 0, 0, 1 }, { 1.0f, 1.0f } },
        { {  kHalfSize, kY, -kHalfSize }, { 0, 1, 0 }, { 1, 0, 0, 1 }, { 1.0f, 0.0f } },
    };

    // 上から見て時計回り = 表面(バックフェイスカリングが有効なので重要)
    const uint32_t indices[] = { 0, 1, 2,  0, 2, 3 };

    m_floorIndexCount = _countof(indices);
    const UINT vertexBufferSize = sizeof(vertices);
    const UINT indexBufferSize = sizeof(indices);

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Width = vertexBufferSize;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    if (!Check(m_device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &resourceDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&m_floorVertexBuffer))))
    {
        return false;
    }

    void* mapped = nullptr;
    D3D12_RANGE readRange = { 0, 0 };
    if (!Check(m_floorVertexBuffer->Map(0, &readRange, &mapped))) return false;
    memcpy(mapped, vertices, vertexBufferSize);
    m_floorVertexBuffer->Unmap(0, nullptr);

    m_floorVertexBufferView.BufferLocation = m_floorVertexBuffer->GetGPUVirtualAddress();
    m_floorVertexBufferView.StrideInBytes = sizeof(Vertex);
    m_floorVertexBufferView.SizeInBytes = vertexBufferSize;

    resourceDesc.Width = indexBufferSize;
    if (!Check(m_device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &resourceDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&m_floorIndexBuffer))))
    {
        return false;
    }

    void* mappedIndex = nullptr;
    if (!Check(m_floorIndexBuffer->Map(0, &readRange, &mappedIndex))) return false;
    memcpy(mappedIndex, indices, indexBufferSize);
    m_floorIndexBuffer->Unmap(0, nullptr);

    m_floorIndexBufferView.BufferLocation = m_floorIndexBuffer->GetGPUVirtualAddress();
    m_floorIndexBufferView.SizeInBytes = indexBufferSize;
    m_floorIndexBufferView.Format = DXGI_FORMAT_R32_UINT;

    return true;
}

//----------------------------------------------------------------------------
// シャドウマップ(深度専用テクスチャ)
//----------------------------------------------------------------------------
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

//----------------------------------------------------------------------------
// 深度専用の PSO
//----------------------------------------------------------------------------
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

//----------------------------------------------------------------------------
// HDR 中間バッファ
//----------------------------------------------------------------------------
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

//----------------------------------------------------------------------------
// トーンマップパス
//----------------------------------------------------------------------------
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

//----------------------------------------------------------------------------
// ブルームの作業用バッファ(1/4 解像度 × 2 枚)
//
// 2 枚あるのは、ぼかしを「A から読んで B に書く → B から読んで A に書く」と
// 交互に行うため。同じテクスチャを読みながら書くことはできない
//----------------------------------------------------------------------------
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

//----------------------------------------------------------------------------
// ブルームの PSO(輝度抽出とぼかし)
//
// 2 つのパスは入出力の形が同じなので、ルートシグネチャを共有できる
//----------------------------------------------------------------------------
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

//----------------------------------------------------------------------------
// 定数バッファ
//----------------------------------------------------------------------------
bool Renderer::CreateConstantBuffer()
{
    // 1 オブジェクトぶんのサイズ。256 バイト境界に揃える必要がある
    m_cbStride = (sizeof(SceneConstants) + 255) & ~255u;

    // 描画要求されたメッシュ群と、床 1 個ぶんを確保する。
    m_objectCount = kMaxDrawMeshCount + 1;
    const UINT bufferSize = m_cbStride * m_objectCount;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Width = bufferSize;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    if (!Check(m_device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &resourceDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&m_constantBuffer))))
    {
        return false;
    }

    // Map したポインタを Unmap せずに保持する。
    // 毎フレーム memcpy するだけで GPU に値が届く。
    D3D12_RANGE readRange = { 0, 0 };   // CPU からは読まない宣言
    return Check(m_constantBuffer->Map(0, &readRange, &m_cbMapped));
}


//----------------------------------------------------------------------------
// 毎フレームの描画
//----------------------------------------------------------------------------
void Renderer::DrawModel(ModelHandle modelHandle, const XMFLOAT4X4& world)
{
    // 現在GPUへ転送済みのモデルだけを受け付ける。
    if (modelHandle.value != m_modelHandle.value) return;
    if ((m_drawCommands.size() + 1) * m_meshes.size() > kMaxDrawMeshCount) return;

    m_drawCommands.push_back({ modelHandle, world });
}

void Renderer::Render()
{
    // --- フレーム時間の計測 ---
    LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
    m_deltaTime = float((uint64_t)now.QuadPart - m_lastCounter) / 
		float(m_perfFrequency); // 前フレームからの経過時間(秒)
	m_lastCounter = (uint64_t)now.QuadPart; // 次フレームのために保存

    // ブレークポイントで長時間止めた直後などに巨大な値が入ると
    // 物体が吹き飛ぶので上限を設けておく(ゲーム開発では定番の保険)
    if (m_deltaTime > 0.1f) m_deltaTime = 0.1f;

    // --- 定数バッファの更新 ---
    // 毎フレーム角度を進める。
    // ※ 本当はフレーム時間を掛けるべき(次のレッスンでタイマーを入れる)
    m_rotationAngle += XMConvertToRadians(45.0f) * m_deltaTime;

    // World 行列: 物体をワールド空間に配置する。
    // Y 軸(縦軸)まわりに回すと、遠近感が付いたことが分かりやすい
    XMMATRIX world = XMMatrixIdentity();

    // View 行列: カメラ。
       // カメラをモデルのまわりで周回させる。
    // 「カメラを動かす」といっても、ビュー行列を作り直しているだけ
    const float orbitRadius = 2.5f;
    XMVECTOR eye = XMVectorSet(sinf(m_rotationAngle) * orbitRadius,
        0.8f,
        -cosf(m_rotationAngle) * orbitRadius, 1.0f);
    XMVECTOR target = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);   // 注視点
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);  // 上方向
    XMMATRIX view = XMMatrixLookAtLH(eye, target, up);

    // Projection 行列: 透視投影。
    //   fovY   : 垂直画角。60度くらいが自然。広げると魚眼寄りになる
    //   aspect : 画面の縦横比。これが歪みを補正している本体
    //   near/far: 描画する深度の範囲。near を小さくし過ぎると
    //             深度バッファの精度が落ちる(Zファイティングの原因)
    const float fovY = XMConvertToRadians(60.0f);
    const float aspect = (float)m_width / (float)m_height;
    // near を大きくすると深度バッファの精度が上がる。
    // 0.1 → 0.5 にするだけで、遠くの壁の Z ファイティングが目に見えて減る
    XMMATRIX proj = XMMatrixPerspectiveFovLH(fovY, aspect, 0.5f, 100.0f);

    // DirectXMath は行ベクトル規約なので、適用したい順に左から掛ける
    XMMATRIX mvp = world * view * proj;

    SceneConstants cb;
    XMStoreFloat4x4(&cb.mvp, XMMatrixTranspose(mvp));    // HLSL は列優先なので転置
    XMStoreFloat4x4(&cb.world, XMMatrixTranspose(world));  // 法線変換用

    // ビュー射影行列の逆行列。
    // スカイボックスが各ピクセルの視線方向を復元するのに使う
    XMVECTOR det;
    XMMATRIX invViewProj = XMMatrixInverse(&det, view * proj);
    XMStoreFloat4x4(&cb.invViewProj, XMMatrixTranspose(invViewProj));


    // カメラ位置。鏡面反射は「どこから見ているか」で見え方が変わるので必須
    XMStoreFloat3(&cb.cameraPosition, eye);

    // 平行光源(太陽のようなもの)。位置を持たず「向き」だけを持つ
    XMVECTOR lightDir = XMVector3Normalize(XMVectorSet(-0.6f, -0.55f, 0.15f, 0.0f));

    // --- ライト視点のビュー射影行列(シャドウマップ用) ---
    // 平行光源は「無限遠から平行に降る光」なので、射影は直交投影を使う。
    // 透視投影と違い、距離によって影の大きさが変わらない
    const XMVECTOR sceneCenter = XMVectorSet(0.0f, 5.0f, 0.0f, 1.0f);
    const XMVECTOR lightEye =
        XMVectorSubtract(sceneCenter, XMVectorScale(lightDir, 40.0f));

    XMMATRIX lightView = XMMatrixLookAtLH(lightEye, sceneCenter,
        XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
    // シーン全体(対角約 35 ユニット)を覆う範囲。
    // 広げるほど 1 テクセルが担当する面積が増えて影が粗くなるので、
    // 「全部入る最小の広さ」を狙うのが定石
    XMMATRIX lightProj = XMMatrixOrthographicLH(40.0f, 40.0f, 0.1f, 100.0f);
    XMMATRIX lightViewProj = lightView * lightProj;

    XMStoreFloat3(&cb.lightDirection, lightDir);

    // PBR では拡散項を 1/PI で正規化するため、ライトの強さは 1.0 では暗すぎる。
    // 「明るさ = 色 × 強度」として分けて考えるのが扱いやすい
    const float lightIntensity = 3.0f;
    cb.lightColor = { 1.00f * lightIntensity,
                      0.97f * lightIntensity,
                      0.92f * lightIntensity };

    // 環境光。次のレッスンで IBL に置き換わる仮の値
    cb.ambientColor = { 0.10f, 0.11f, 0.14f };

    XMStoreFloat4x4(&cb.lightViewProj, XMMatrixTranspose(lightViewProj));

    // --- DrawModel で登録された各モデルインスタンスの定数を書く ---
    size_t drawConstantIndex = 0;
    for (const DrawCommand& command : m_drawCommands)
    {
        const XMMATRIX objectWorld = XMLoadFloat4x4(&command.world);
        XMStoreFloat4x4(&cb.mvp, XMMatrixTranspose(objectWorld * view * proj));
        XMStoreFloat4x4(&cb.world, XMMatrixTranspose(objectWorld));

        for (size_t i = 0; i < m_meshes.size(); ++i)
        {
            const int matIndex = m_meshes[i].material;
            if (matIndex >= 0 && (size_t)matIndex < m_model->materials.size())
            {
                const GltfMaterial& mat = m_model->materials[(size_t)matIndex];
                cb.albedo = { mat.baseColorFactor[0], mat.baseColorFactor[1],
                    mat.baseColorFactor[2] };
                cb.metallic = mat.metallicFactor;
                cb.roughness = mat.roughnessFactor;
                const float kEmissiveStrength = 4.0f;
                cb.emissiveFactor = { mat.emissiveFactor[0] * kEmissiveStrength,
                    mat.emissiveFactor[1] * kEmissiveStrength,
                    mat.emissiveFactor[2] * kEmissiveStrength };
            }
            else
            {
                cb.albedo = { 1.0f, 1.0f, 1.0f };
                cb.metallic = 0.0f;
                cb.roughness = 0.5f;
                cb.emissiveFactor = { 0.0f, 0.0f, 0.0f };
            }

            cb.alphaCutoff = m_meshes[i].alphaCutoff;
            cb.padding8 = cb.padding9 = cb.padding10 = 0.0f;
            memcpy((uint8_t*)m_cbMapped + drawConstantIndex * m_cbStride,
                &cb, sizeof(cb));
            ++drawConstantIndex;
        }
    }

    // --- 床(固定の最終スロット) ---
    const size_t floorConstantIndex = kMaxDrawMeshCount;
    XMStoreFloat4x4(&cb.mvp, XMMatrixTranspose(world * view * proj));
    XMStoreFloat4x4(&cb.world, XMMatrixTranspose(world));
    cb.albedo = { 0.18f, 0.18f, 0.19f };
    cb.alphaCutoff = -1.0f;
    cb.metallic = 0.0f;
    cb.roughness = 0.70f;
    cb.emissiveFactor = { 0.0f, 0.0f, 0.0f };
    memcpy((uint8_t*)m_cbMapped + floorConstantIndex * m_cbStride, &cb, sizeof(cb));

    // 1. このフレーム用のアロケータを Reset。
    //    (WaitForGpu 済み or フェンスで安全が確認できているときだけ許される)
    ID3D12CommandAllocator* allocator = m_commandAllocators[m_frameIndex].Get();
    allocator->Reset();
    m_commandList->Reset(allocator, m_pipelineState.Get());

    // ===== シャドウマップ生成パス =====
// ライト視点から深度だけを描く。本描画より前に済ませておく必要がある
    {
        D3D12_RESOURCE_BARRIER shadowBarrier = {};
        shadowBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        shadowBarrier.Transition.pResource = m_shadowMap.Get();
        shadowBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        shadowBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        shadowBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        m_commandList->ResourceBarrier(1, &shadowBarrier);

        D3D12_CPU_DESCRIPTOR_HANDLE shadowDsv =
            m_shadowDsvHeap->GetCPUDescriptorHandleForHeapStart();
        // カラーの出力先は無し。深度だけを描くのでレンダーターゲットは 0 個
        m_commandList->OMSetRenderTargets(0, nullptr, FALSE, &shadowDsv);
        m_commandList->ClearDepthStencilView(shadowDsv, D3D12_CLEAR_FLAG_DEPTH,
            1.0f, 0, 0, nullptr);

        // ビューポートは画面ではなくシャドウマップの解像度
        D3D12_VIEWPORT shadowVp =
        { 0.0f, 0.0f, (float)kShadowMapSize, (float)kShadowMapSize, 0.0f, 1.0f };
        D3D12_RECT shadowSc = { 0, 0, (LONG)kShadowMapSize, (LONG)kShadowMapSize };
        m_commandList->RSSetViewports(1, &shadowVp);
        m_commandList->RSSetScissorRects(1, &shadowSc);

        m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
        // ディスクリプタテーブルを使う前に、必ずヒープをバインドする。
        // これはコマンドリスト単位の状態で、ルートシグネチャとは独立している
        ID3D12DescriptorHeap* shadowHeaps[] = { m_srvHeap.Get() };
        m_commandList->SetDescriptorHeaps(_countof(shadowHeaps), shadowHeaps);

        m_commandList->SetPipelineState(m_shadowPSO.Get());
        m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // 登録された全モデルインスタンスが影を落とす。
        drawConstantIndex = 0;
        for (const DrawCommand& command : m_drawCommands)
        {
            (void)command;
            for (size_t i = 0; i < m_meshes.size(); ++i)
            {
                m_commandList->SetGraphicsRootConstantBufferView(0,
                    m_constantBuffer->GetGPUVirtualAddress() + drawConstantIndex * m_cbStride);
                const UINT materialSlot = (m_meshes[i].material >= 0)
                    ? kSrvMaterialStart + (UINT)m_meshes[i].material * kTexturesPerMaterial
                    : kSrvDefaultMaterial;
                D3D12_GPU_DESCRIPTOR_HANDLE shadowMaterial =
                    m_srvHeap->GetGPUDescriptorHandleForHeapStart();
                shadowMaterial.ptr += (SIZE_T)materialSlot *
                    m_device->GetDescriptorHandleIncrementSize(
                        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                m_commandList->SetGraphicsRootDescriptorTable(2, shadowMaterial);
                m_commandList->IASetVertexBuffers(0, 1, &m_meshes[i].vertexBufferView);
                m_commandList->IASetIndexBuffer(&m_meshes[i].indexBufferView);
                m_commandList->DrawIndexedInstanced(m_meshes[i].indexCount, 1, 0, 0, 0);
                ++drawConstantIndex;
            }
        }

        // 深度書き込み → テクスチャとして読める状態へ戻す
        shadowBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        shadowBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        m_commandList->ResourceBarrier(1, &shadowBarrier);
    }

    // 2. HDR バッファを描画先にする。
    //    バックバッファ(8bit)に直接描くと 1.0 を超える明るさがその場で
    //    失われてしまうため、シーンは 16bit 浮動小数のバッファへ描く
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.pResource = m_hdrBuffer.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    m_commandList->ResourceBarrier(1, &barrier);

    // 3. レンダーターゲット設定 + クリア
    D3D12_CPU_DESCRIPTOR_HANDLE hdrRtv =
        m_hdrRtvHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle =
        m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
    m_commandList->OMSetRenderTargets(1, &hdrRtv, FALSE, &dsvHandle);

    // スカイボックスが全画面を塗るので色のクリアは本来不要だが、
    // 未初期化のメモリを読む事故を防ぐために入れておく
    const float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };
    m_commandList->ClearRenderTargetView(hdrRtv, clearColor, 0, nullptr);
    m_commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH,
        1.0f, 0, 0, nullptr);

    // 4. ビューポート/シザー(PSO には含まれないので毎フレーム設定)
    D3D12_VIEWPORT viewport = { 0.0f, 0.0f, (float)m_width, (float)m_height, 0.0f, 1.0f };
    D3D12_RECT scissor      = { 0, 0, (LONG)m_width, (LONG)m_height };
    m_commandList->RSSetViewports(1, &viewport);
    m_commandList->RSSetScissorRects(1, &scissor);

    // --- 背景(スカイボックス)を先に描く ---
    m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());

    // シェーダから見えるヒープをバインドする。
    // これはコマンドリスト単位の状態なので、描画ごとに設定し直す必要はない
    ID3D12DescriptorHeap* shadowHeaps[] = { m_srvHeap.Get() };
    m_commandList->SetDescriptorHeaps(_countof(shadowHeaps), shadowHeaps);

    m_commandList->SetGraphicsRootConstantBufferView(
        0, m_constantBuffer->GetGPUVirtualAddress() + floorConstantIndex * m_cbStride);
    // ルートパラメータ 1 番に、SRV が置いてあるヒープ上の位置を渡す
    m_commandList->SetGraphicsRootDescriptorTable(
        1, m_srvHeap->GetGPUDescriptorHandleForHeapStart());

    // スカイボックスは t4〜t8 を使わないが、
    // ルートパラメータは全部設定しておくのが安全
    D3D12_GPU_DESCRIPTOR_HANDLE skyMaterial = m_srvHeap->GetGPUDescriptorHandleForHeapStart();
    skyMaterial.ptr += kSrvDefaultMaterial * m_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_commandList->SetGraphicsRootDescriptorTable(2, skyMaterial);

    // ルートパラメータ 3 番にシャドウマップ(ヒープ 14 番)
    D3D12_GPU_DESCRIPTOR_HANDLE shadowSrv =
        m_srvHeap->GetGPUDescriptorHandleForHeapStart();
    shadowSrv.ptr += kSrvShadowMap * m_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_commandList->SetGraphicsRootDescriptorTable(3, shadowSrv);

    m_commandList->SetPipelineState(m_skyboxPSO.Get());
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    // 頂点バッファなしで 3 頂点。シェーダが SV_VertexID から座標を作る
    m_commandList->DrawInstanced(3, 1, 0, 0);

    // --- ここからモデル ---
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    const UINT srvStep = m_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    const D3D12_GPU_DESCRIPTOR_HANDLE heapStart =
        m_srvHeap->GetGPUDescriptorHandleForHeapStart();
    const D3D12_GPU_VIRTUAL_ADDRESS cbBase =
        m_constantBuffer->GetGPUVirtualAddress();

    // --- 登録されたモデルインスタンスの全メッシュ ---
    drawConstantIndex = 0;
    for (const DrawCommand& command : m_drawCommands)
    {
        (void)command;
        for (size_t i = 0; i < m_meshes.size(); ++i)
        {
            const MeshPrimitive& mesh = m_meshes[i];
            m_commandList->SetPipelineState(mesh.doubleSided
                ? m_pipelineStateNoCull.Get()
                : m_pipelineState.Get());
            m_commandList->SetGraphicsRootConstantBufferView(
                0, cbBase + drawConstantIndex * m_cbStride);
            const UINT materialSlot = (mesh.material >= 0)
                ? kSrvMaterialStart + (UINT)mesh.material * kTexturesPerMaterial
                : kSrvDefaultMaterial;
            D3D12_GPU_DESCRIPTOR_HANDLE material = heapStart;
            material.ptr += (SIZE_T)materialSlot * srvStep;
            m_commandList->SetGraphicsRootDescriptorTable(2, material);
            m_commandList->IASetVertexBuffers(0, 1, &mesh.vertexBufferView);
            m_commandList->IASetIndexBuffer(&mesh.indexBufferView);
            m_commandList->DrawIndexedInstanced(mesh.indexCount, 1, 0, 0, 0);
            ++drawConstantIndex;
        }
    }

    // --- 床(定数 1 番、マテリアルはヒープ 9 番から) ---
    {
        m_commandList->SetGraphicsRootConstantBufferView(
            0, cbBase + floorConstantIndex * m_cbStride);

        D3D12_GPU_DESCRIPTOR_HANDLE material = heapStart;
        material.ptr += kSrvDefaultMaterial * srvStep;
        m_commandList->SetGraphicsRootDescriptorTable(2, material);

        m_commandList->IASetVertexBuffers(0, 1, &m_floorVertexBufferView);
        m_commandList->IASetIndexBuffer(&m_floorIndexBufferView);
        m_commandList->DrawIndexedInstanced(m_floorIndexCount, 1, 0, 0, 0);
    }

    // ===== ブルーム =====
    {
        // HDR バッファ: 描画先 → テクスチャとして読める状態へ
        barrier.Transition.pResource = m_hdrBuffer.Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        m_commandList->ResourceBarrier(1, &barrier);

        const UINT srvStep = m_device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        const UINT rtvStep = m_device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        auto srvOf = [&](UINT index)
            {
                D3D12_GPU_DESCRIPTOR_HANDLE h = m_srvHeap->GetGPUDescriptorHandleForHeapStart();
                h.ptr += (SIZE_T)index * srvStep;
                return h;
            };
        auto bloomRtvOf = [&](UINT index)
            {
                D3D12_CPU_DESCRIPTOR_HANDLE h = m_bloomRtvHeap->GetCPUDescriptorHandleForHeapStart();
                h.ptr += (SIZE_T)index * rtvStep;
                return h;
            };
        // A と B の状態を入れ替えるバリアを短く書くためのヘルパ
        auto transition = [&](ID3D12Resource* res,
            D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to)
            {
                barrier.Transition.pResource = res;
                barrier.Transition.StateBefore = from;
                barrier.Transition.StateAfter = to;
                m_commandList->ResourceBarrier(1, &barrier);
            };

        const D3D12_RESOURCE_STATES kRead = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        const D3D12_RESOURCE_STATES kWrite = D3D12_RESOURCE_STATE_RENDER_TARGET;

        m_commandList->SetGraphicsRootSignature(m_bloomRootSignature.Get());
        m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        // ビューポートは 1/4 解像度
        D3D12_VIEWPORT bloomVp =
        { 0.0f, 0.0f, (float)m_bloomWidth, (float)m_bloomHeight, 0.0f, 1.0f };
        D3D12_RECT bloomSc = { 0, 0, (LONG)m_bloomWidth, (LONG)m_bloomHeight };
        m_commandList->RSSetViewports(1, &bloomVp);
        m_commandList->RSSetScissorRects(1, &bloomSc);

        // --- 1. 輝度抽出: HDR(フル解像度) → A(1/4 解像度) ---
        transition(m_bloomA.Get(), kRead, kWrite);
        D3D12_CPU_DESCRIPTOR_HANDLE rtvA = bloomRtvOf(0);
        m_commandList->OMSetRenderTargets(1, &rtvA, FALSE, nullptr);

        m_commandList->SetPipelineState(m_bloomBrightPSO.Get());
        const float brightParams[4] =
        {
            m_bloomThreshold, 0.0f,
            1.0f / (float)m_width, 1.0f / (float)m_height   // 入力のテクセルサイズ
        };
        m_commandList->SetGraphicsRoot32BitConstants(0, 4, brightParams, 0);
        m_commandList->SetGraphicsRootDescriptorTable(1, srvOf(kSrvHdrBuffer));   // HDR バッファ
        m_commandList->DrawInstanced(3, 1, 0, 0);

        // --- 2. 横 → 縦 のぼかしを 2 往復 ---
        // 回数を増やすほど滲みが広がる
        m_commandList->SetPipelineState(m_bloomBlurPSO.Get());

        const float texelX = 1.0f / (float)m_bloomWidth;
        const float texelY = 1.0f / (float)m_bloomHeight;

        for (int i = 0; i < 2; ++i)
        {
            // 横方向: A → B
            transition(m_bloomA.Get(), kWrite, kRead);
            transition(m_bloomB.Get(), kRead, kWrite);

            D3D12_CPU_DESCRIPTOR_HANDLE rtvB = bloomRtvOf(1);
            m_commandList->OMSetRenderTargets(1, &rtvB, FALSE, nullptr);
            const float horizontal[4] = { texelX, 0.0f, 0.0f, 0.0f };
            m_commandList->SetGraphicsRoot32BitConstants(0, 4, horizontal, 0);
            m_commandList->SetGraphicsRootDescriptorTable(1, srvOf(kSrvBloomA));   // A
            m_commandList->DrawInstanced(3, 1, 0, 0);

            // 縦方向: B → A
            transition(m_bloomB.Get(), kWrite, kRead);
            transition(m_bloomA.Get(), kRead, kWrite);

            D3D12_CPU_DESCRIPTOR_HANDLE rtvA2 = bloomRtvOf(0);
            m_commandList->OMSetRenderTargets(1, &rtvA2, FALSE, nullptr);
            const float vertical[4] = { 0.0f, texelY, 0.0f, 0.0f };
            m_commandList->SetGraphicsRoot32BitConstants(0, 4, vertical, 0);
            m_commandList->SetGraphicsRootDescriptorTable(1, srvOf(kSrvBloomB));   // B
            m_commandList->DrawInstanced(3, 1, 0, 0);
        }

        // 最終結果は A にある。読める状態に戻す
        transition(m_bloomA.Get(), kWrite, kRead);
    }

    // ===== 6. トーンマップパス(HDR バッファ → バックバッファ) =====
    {
        // バックバッファ: 表示用 → 描画先へ
        barrier.Transition.pResource = m_renderTargets[m_frameIndex].Get();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        m_commandList->ResourceBarrier(1, &barrier);

        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
            m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtvHandle.ptr += (SIZE_T)m_frameIndex * m_rtvDescriptorSize;
        // 全画面を塗り替えるので、深度もクリアも不要
        m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

        // ビューポートはコマンドリストの状態として残り続けるので、
        // ブルームで 1/4 解像度に変えたぶんを画面サイズへ戻す
        D3D12_VIEWPORT fullVp =
        { 0.0f, 0.0f, (float)m_width, (float)m_height, 0.0f, 1.0f };
        D3D12_RECT fullSc = { 0, 0, (LONG)m_width, (LONG)m_height };
        m_commandList->RSSetViewports(1, &fullVp);
        m_commandList->RSSetScissorRects(1, &fullSc);

        m_commandList->SetGraphicsRootSignature(m_tonemapRootSignature.Get());
        m_commandList->SetPipelineState(m_tonemapPSO.Get());

        // 露出をルート定数で渡す
        const float tonemapParams[2] = { m_exposure, m_bloomIntensity };
        m_commandList->SetGraphicsRoot32BitConstants(0, 2, tonemapParams, 0);

        D3D12_GPU_DESCRIPTOR_HANDLE hdrSrv =
            m_srvHeap->GetGPUDescriptorHandleForHeapStart();
        hdrSrv.ptr += kSrvHdrBuffer * m_device->GetDescriptorHandleIncrementSize(
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        m_commandList->SetGraphicsRootDescriptorTable(1, hdrSrv);

        m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_commandList->DrawInstanced(3, 1, 0, 0);

        // バックバッファ: 描画先 → 表示用へ
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        m_commandList->ResourceBarrier(1, &barrier);
    }

    // 7. 記録終了 → キューへ投入
    m_commandList->Close();
    ID3D12CommandList* lists[] = { m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(1, lists);

    // 8. 表示(垂直同期あり)
    m_swapChain->Present(1, 0);

    // 9. フレーム同期:
    //    今投げたフレームの完了目標値をキューに Signal し、
    //    「次に使うバックバッファ」のフェンス値がまだなら待つ。
    //    (2 枚バッファなので、実質 1 フレーム前の完了を待つことになる)
    const uint64_t currentFenceValue = m_fenceValues[m_frameIndex];
    m_commandQueue->Signal(m_fence.Get(), currentFenceValue);

    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

    if (m_fence->GetCompletedValue() < m_fenceValues[m_frameIndex])
    {
        m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent);
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
    m_fenceValues[m_frameIndex] = currentFenceValue + 1;
    m_drawCommands.clear();
}

//----------------------------------------------------------------------------
// GPU の全処理完了を待つ(終了時・リサイズ時に使う)
//----------------------------------------------------------------------------
void Renderer::WaitForGpu()
{
    const uint64_t fenceValue = m_fenceValues[m_frameIndex];
    m_commandQueue->Signal(m_fence.Get(), fenceValue);

    m_fence->SetEventOnCompletion(fenceValue, m_fenceEvent);
    WaitForSingleObject(m_fenceEvent, INFINITE);

    m_fenceValues[m_frameIndex] = fenceValue + 1;
}
