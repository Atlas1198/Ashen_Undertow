//-------------------------------------------------
//
//          Manager.h
//
//  入力、シーン、音声、描画を統括する最上位クラスを定義する。
//-------------------------------------------------

#pragma once

#include <Windows.h>
#include <chrono>
#include <cstdint>
#include <memory>

#include "Renderer.h"
#include "SceneManager.h"   
#include "ModelManager.h"
#include "Input.h"
#include "UIManager.h"
#include "AudioManager.h"

// ゲーム全体の最上層。main.cpp はこのクラスだけを操作する。
class Manager
{
public:
    bool Initialize(HWND hwnd, uint32_t width, uint32_t height);
    // 入力 → シーン → 描画の順に一フレームを進め、遷移要求を処理する。
    void RunFrame();

private:
    ModelManager m_modelManager;
    Input m_input;
    Renderer m_renderer;
    UIManager m_uiManager;
    AudioManager m_audioManager;
    // 現在のシーンだけを所有する。切り替え時に前シーンを破棄する。
    std::unique_ptr<SceneManager> m_currentScene;
    std::chrono::steady_clock::time_point m_lastFrameTime;
};
