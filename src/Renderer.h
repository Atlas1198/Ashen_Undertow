//----------------------------------------------------------------------------
// Renderer.h
// DX12 の初期化と毎フレームの描画をまとめたクラス。
//
// DX12 の全体像(このクラスが持っているもの):
//   Device        : GPU との窓口。あらゆるオブジェクトの生成器
//   CommandQueue  : GPU に仕事(コマンドリスト)を投げる列
//   SwapChain     : 画面に出すバッファ(バックバッファ)を 2 枚持ち、交互に表示
//   CommandList   : 「これ描け」「バリア張れ」などの命令を記録する
//   Fence         : CPU と GPU の同期を取る仕組み
//   RootSignature : シェーダに渡すリソースの「配置表」
//   PSO           : シェーダ + 各種固定機能設定を一塊に焼き固めたもの
//----------------------------------------------------------------------------
#pragma once

#include <vector>
#include <DirectXMath.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>   // ComPtr (COM オブジェクトのスマートポインタ)
#include <cstdint>
#include "HdrLoader.h"
#include "GltfLoader.h"
#include "ModelManager.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;

class Renderer
{
public:
    ~Renderer();

    bool Initialize(HWND hwnd, uint32_t width, uint32_t height, ModelManager& modelManager);
    void SetCameraTarget(const DirectX::XMFLOAT3& target) { m_cameraTarget = target; }
    void DrawModel(ModelHandle modelHandle, const DirectX::XMFLOAT4X4& world);
    void Render();

private:
    // 初期化を段階ごとに分けたもの。Initialize から順番に呼ばれる。
    bool CreateDevice();
    bool CreateCommandObjects();
    bool CreateSwapChain(HWND hwnd);
    bool CreateRenderTargets();
    bool CreateFence();
    bool CreateTrianglePipeline();   // ルートシグネチャ + シェーダ + PSO
    bool CreateVertexBuffer();
    bool CreateMaterialTextures();   // モデルのテクスチャを GPU に載せる
    bool CreateFloor();   // 影を受ける床
    bool CreateShadowMap();        // 深度専用のテクスチャ
    bool CreateShadowPipeline();   // 深度だけを描く PSO
    bool CreateHdrBuffer();         // シーンを描く 16bit 中間バッファ
    bool CreateTonemapPipeline();   // HDR → バックバッファ の変換パス
    bool CreateBloomResources();    // 1/4 解像度の作業用バッファ 2 枚
    bool CreateBloomPipelines();    // 輝度抽出とぼかしの PSO

    // テクスチャ 1 枚を GPU に転送する共通処理。
    // ピクセルは RGBA8(1 ピクセル 4 バイト)であることを前提とする
    bool UploadTexture(const void* pixels, UINT width, UINT height,
        DXGI_FORMAT format, ComPtr<ID3D12Resource>& outResource);
    bool CreateConstantBuffer();
    bool CreateDepthBuffer();  // 深度バッファ
	bool CreateEnvironmentTexture(); // 環境マップ
	bool CreateSkyboxPipeline(); // 背景描画用のPSO
	bool CreateIrradianceMap(); // 環境マップから生成する放射輝度マップ
    bool CreatePrefilterMap();  // roughness 別にぼかした鏡面 IBL 用マップ
    bool CreateBrdfLut();  // BRDF 積分の事前計算テーブル

    // GPU が今投げてある仕事を全部終えるまで CPU を待たせる
    void WaitForGpu();

    // バックバッファは 2 枚でダブルバッファリング
    static constexpr uint32_t kFrameCount = 2;
    static constexpr uint32_t kMaxDrawMeshCount = 4096;

    // 定数バッファに載せるオブジェクト数(メッシュ数 + 床)。
    // モデル読み込み後に決まるので実行時に求める
    uint32_t m_objectCount = 0;

    // シャドウマップの解像度。上げるほど影の輪郭が精細になるが重くなる
    static constexpr uint32_t kShadowMapSize = 2048;

    // --- SRV ディスクリプタヒープのレイアウト ---
    // 番号を直接書くとマテリアルが増えたときに破綻するので、
    // 意味のある名前で一箇所に集約する。
    // 可変長のマテリアルを末尾に置くのがポイント
    static constexpr uint32_t kSrvIbl = 0;   // 0〜3 : t0〜t3
    static constexpr uint32_t kSrvShadowMap = 4;   // t9
    static constexpr uint32_t kSrvHdrBuffer = 5;   // トーンマップの t0
    // トーンマップは t0/t1 を 1 つのテーブルで受け取るので、
    // kSrvBloomA は kSrvHdrBuffer の直後でなければならない
    static constexpr uint32_t kSrvBloomA = 6;   // トーンマップの t1
    static constexpr uint32_t kSrvBloomB = 7;
    static constexpr uint32_t kSrvDefaultMaterial = 8;   // 8〜12 : テクスチャ無し用
    static constexpr uint32_t kSrvMaterialStart = 13;  // 13〜 : 5 個 × マテリアル数
    static constexpr uint32_t kTexturesPerMaterial = 5;   // baseColor/MR/normal/AO/emissive

    uint32_t m_width  = 0;
    uint32_t m_height = 0;
    DirectX::XMFLOAT3 m_cameraTarget { 0.0f, 0.0f, 0.0f };
    HWND m_window = nullptr;
    POINT m_lastMousePosition {};
    bool m_cameraDragging = false;
    bool m_cameraControlEnabled = true;
    bool m_leftMouseWasDown = false;
    int m_cursorHideCalls = 0;
    float m_cameraYaw = 0.0f;
    float m_cameraPitch = 0.39479112f; // atan2(2.5, 6): 初期の視点を維持

    // --- コアオブジェクト ---
    ComPtr<IDXGIFactory6>              m_factory;
    ComPtr<ID3D12Device>               m_device;
    ComPtr<ID3D12CommandQueue>         m_commandQueue;
    ComPtr<IDXGISwapChain4>            m_swapChain;

    // --- レンダーターゲット ---
    ComPtr<ID3D12DescriptorHeap>       m_rtvHeap;                    // RTV 用ディスクリプタヒープ
    ComPtr<ID3D12Resource>             m_renderTargets[kFrameCount]; // バックバッファ本体
    uint32_t                           m_rtvDescriptorSize = 0;      // RTV 1個分のサイズ(GPU依存)

    // --- 深度バッファ ---
     // 深度は「毎フレーム使い捨て」なので、バックバッファと違って 1 枚でよい
	ComPtr<ID3D12DescriptorHeap>       m_dsvHeap;                    // DSV 用ディスクリプタヒープ
	ComPtr<ID3D12Resource>             m_depthBuffer;                // 深度バッファ本体

    // --- コマンド記録 ---
    // アロケータ = コマンドの記録先メモリ。GPU が実行し終わるまで Reset できないので
    // フレーム数分持つ(フレーム N を GPU が描いている間にフレーム N+1 を記録するため)
    ComPtr<ID3D12CommandAllocator>     m_commandAllocators[kFrameCount];
    ComPtr<ID3D12GraphicsCommandList>  m_commandList;

    // --- CPU/GPU 同期 ---
    ComPtr<ID3D12Fence>                m_fence;
    uint64_t                           m_fenceValues[kFrameCount] = {};
    HANDLE                             m_fenceEvent = nullptr;
    uint32_t                           m_frameIndex = 0;   // 今描き込むバックバッファ番号

    // --- 三角形描画用 ---
    ComPtr<ID3D12RootSignature>        m_rootSignature;
    ComPtr<ID3D12PipelineState>        m_pipelineState;
    ComPtr<ID3D12PipelineState>        m_pipelineStateNoCull;   // 両面マテリアル用

    // glTF のプリミティブ 1 個ぶん。
    // マテリアルごとに描画をまとめる必要があるので、
    // 1 つの巨大なバッファではなくプリミティブ単位で持つ
    struct MeshPrimitive
    {
        ComPtr<ID3D12Resource>   vertexBuffer;
        ComPtr<ID3D12Resource>   indexBuffer;
        D3D12_VERTEX_BUFFER_VIEW vertexBufferView = {};
        D3D12_INDEX_BUFFER_VIEW  indexBufferView = {};
        UINT                     indexCount = 0;
        int                      material = -1;   // -1 なら既定マテリアル
        float                    alphaCutoff = -1.0f;   // 負なら常に通る(不透明)
        bool                     doubleSided = false;
    };
    std::vector<MeshPrimitive> m_meshes;

    struct DrawCommand
    {
        ModelHandle modelHandle;
        DirectX::XMFLOAT4X4 world;
    };
    std::vector<DrawCommand> m_drawCommands;

    // --- 環境マップ(IBL の元になる HDRI) ---
	ComPtr<ID3D12DescriptorHeap> m_srvHeap;         // SRV 用ディスクリプタヒープ
	ComPtr<ID3D12Resource>       m_envTexture;      // 環境マップ本体
	ComPtr<ID3D12PipelineState> m_skyboxPSO;        // 背景描画用の PSO
    ComPtr<ID3D12Resource>       m_irradianceMap;      // 畳み込み済みの放射照度マップ
    ComPtr<ID3D12DescriptorHeap> m_irradianceRtvHeap;  // それに描き込むための RTV
    ComPtr<ID3D12Resource>       m_prefilterMap;      // ミップごとに roughness が違う
    ComPtr<ID3D12DescriptorHeap> m_prefilterRtvHeap;  // ミップ数ぶんの RTV
    ComPtr<ID3D12Resource>       m_brdfLut;         // split-sum 後半の項のテーブル
    ComPtr<ID3D12DescriptorHeap> m_brdfLutRtvHeap;  // それに描き込むための RTV

    // --- モデル ---
    const GltfModel*       m_model = nullptr;
    ModelHandle            m_modelHandle;
    // 全マテリアルぶんのテクスチャ。
    // SRV はヒープが持っているので、ここは寿命を保つためだけの入れ物
    std::vector<ComPtr<ID3D12Resource>> m_materialTextures;

    // テクスチャを持たないマテリアル用の既定テクスチャ
    ComPtr<ID3D12Resource> m_defaultWhiteSrgb;
    ComPtr<ID3D12Resource> m_defaultWhite;
    ComPtr<ID3D12Resource> m_defaultNormal;

    // --- 床 ---
    ComPtr<ID3D12Resource>   m_floorVertexBuffer;
    ComPtr<ID3D12Resource>   m_floorIndexBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_floorVertexBufferView = {};
    D3D12_INDEX_BUFFER_VIEW  m_floorIndexBufferView = {};
    UINT                     m_floorIndexCount = 0;

    // --- シャドウマップ ---
    ComPtr<ID3D12Resource>       m_shadowMap;
    ComPtr<ID3D12DescriptorHeap> m_shadowDsvHeap;
    ComPtr<ID3D12PipelineState>  m_shadowPSO;

    // --- HDR パイプライン ---
    ComPtr<ID3D12Resource>       m_hdrBuffer;
    ComPtr<ID3D12DescriptorHeap> m_hdrRtvHeap;
    ComPtr<ID3D12RootSignature>  m_tonemapRootSignature;
    ComPtr<ID3D12PipelineState>  m_tonemapPSO;
    float                        m_exposure = 1.3f;   // カメラの絞りにあたる
    float                        m_bloomIntensity = 0.6f;   // ブルームの強さ

    // --- ブルーム ---
    // ぼかすので低解像度で十分。1/4 なら計算量は 1/16 になる
    ComPtr<ID3D12Resource>       m_bloomA;
    ComPtr<ID3D12Resource>       m_bloomB;
    ComPtr<ID3D12DescriptorHeap> m_bloomRtvHeap;
    ComPtr<ID3D12RootSignature>  m_bloomRootSignature;
    ComPtr<ID3D12PipelineState>  m_bloomBrightPSO;
    ComPtr<ID3D12PipelineState>  m_bloomBlurPSO;
    uint32_t                     m_bloomWidth = 0;
    uint32_t                     m_bloomHeight = 0;
    float                        m_bloomThreshold = 2.0f;   // これを超えた明るさが滲む

	// --- 定数バッファ ---
    ComPtr<ID3D12Resource>             m_constantBuffer;
	void*                              m_cbMapped = nullptr;    // MapしたままのCPU書き込み先
    UINT                               m_cbStride = 0;   // 1 オブジェクトぶんの定数バッファサイズ
};
