#include "Manager.h"

#include "Scene_Game.h"

bool Manager::Initialize(HWND hwnd, uint32_t width, uint32_t height)
{
    m_currentScene = std::make_unique<GameScene>(m_modelManager);
    if (!m_currentScene->Initialize())
        return false;

    if (!m_renderer.Initialize(hwnd, width, height, m_modelManager))
        return false;

    m_lastFrameTime = std::chrono::steady_clock::now();
    return true;
}

void Manager::RunFrame()
{
    const auto now = std::chrono::steady_clock::now();
    float deltaTime = std::chrono::duration<float>(now - m_lastFrameTime).count();
    m_lastFrameTime = now;

    // デバッグ停止後などの極端な移動を防ぐ。
    if (deltaTime > 0.1f) deltaTime = 0.1f;

    m_currentScene->Update(deltaTime);
    m_currentScene->Draw(m_renderer);
    m_renderer.Render();
}
