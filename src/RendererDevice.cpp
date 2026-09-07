#include "RendererInternal.h"

using namespace DirectX;
using namespace RendererDetail;

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
