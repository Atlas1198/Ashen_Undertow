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
    void RunFrame();

private:
    ModelManager m_modelManager;
    Input m_input;
    Renderer m_renderer;
    UIManager m_uiManager;
    AudioManager m_audioManager;
    std::unique_ptr<SceneManager> m_currentScene;
    std::chrono::steady_clock::time_point m_lastFrameTime;
};
