//----------------------------------------------------------------------------
// main.cpp
// Win32 ウィンドウの作成とメインループ。
// DX12 そのものはここには出てこない。ウィンドウ(HWND)を Renderer に渡すだけ。
//----------------------------------------------------------------------------
#include <Windows.h>
#include "Renderer.h"

namespace
{
    constexpr wchar_t kWindowClassName[] = L"RenderEngineWindowClass";
    // 16:9。1920x1080 のディスプレイに枠込みで収まるサイズにしている
    constexpr uint32_t kClientWidth  = 1600;
    constexpr uint32_t kClientHeight = 900;
}

// ウィンドウプロシージャ:
// OS から届くメッセージ(閉じる、キー入力など)を処理するコールバック。
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_DESTROY:
        // ウィンドウが破棄されたらメッセージループを終わらせる
        PostQuitMessage(0);
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) { DestroyWindow(hwnd); }
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow)
{
    // 1. ウィンドウクラス登録
    WNDCLASSEX wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kWindowClassName;
    RegisterClassEx(&wc);

    // 2. クライアント領域(描画領域)がちょうど 1280x720 になるように
    //    ウィンドウ枠込みのサイズを逆算する
    RECT rect = { 0, 0, (LONG)kClientWidth, (LONG)kClientHeight };
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    HWND hwnd = CreateWindowEx(
        0, kWindowClassName, L"RenderEngine (DX12)",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, hInstance, nullptr);

    // 3. レンダラ初期化(DX12 の本体は Renderer.cpp)
    Renderer renderer;
    if (!renderer.Initialize(hwnd, kClientWidth, kClientHeight))
    {
        // 失敗の詳細はデバッグレイヤーが「出力」ウィンドウに書いている。
        // Visual Studio の [デバッグ] → [ウィンドウ] → [出力] を必ず確認する
        MessageBox(hwnd,
            L"DX12 の初期化に失敗しました。\n"
            L"Visual Studio の「出力」ウィンドウにデバッグレイヤーの詳細が出ています。",
            L"Error", MB_OK | MB_ICONERROR);
        return -1;
    }

    ShowWindow(hwnd, nCmdShow);

    // 4. メインループ
    //    PeekMessage はメッセージが無ければすぐ戻るので、
    //    空いた時間でひたすら描画できる(ゲームループの基本形)
    MSG msg = {};
    while (msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {
            renderer.Render();
        }
    }

    // 5. 終了処理: GPU がまだコマンドを実行中かもしれないので、
    //    完了を待ってからリソースを解放する(Renderer のデストラクタ内で待つ)
    return (int)msg.wParam;
}
