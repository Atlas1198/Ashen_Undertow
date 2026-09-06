#pragma once

#include <Windows.h>
#include <chrono>
#include <cstdint>
#include <memory>

#include "Renderer.h"
#include "SceneManager.h"
#include "ModelManager.h"

// ゲーム全体の最上層。main.cpp はこのクラスだけを操作する。
class Manager
{
public:
    bool Initialize(HWND hwnd, uint32_t width, uint32_t height);
    void RunFrame();

private:
    ModelManager m_modelManager;
    Renderer m_renderer;
    std::unique_ptr<SceneManager> m_currentScene;
    std::chrono::steady_clock::time_point m_lastFrameTime;
};
