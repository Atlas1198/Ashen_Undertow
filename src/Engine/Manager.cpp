#include "Manager.h"

#include "Scene_Game.h"
#include "Scene_Title.h"
#include "Scene_Tutorial.h"

namespace
{
std::unique_ptr<SceneManager> CreateRequestedScene(SceneRequest request,
    ModelManager& models, const Input& input)
{
    if (request == SceneRequest::Title)
        return std::make_unique<Scene_Title>(models, input);
    if (request == SceneRequest::Tutorial)
        return std::make_unique<Scene_Tutorial>(models, input);
    return std::make_unique<Scene_Game>(models, input);
}
}

bool Manager::Initialize(HWND hwnd, uint32_t width, uint32_t height)
{
    m_input.Initialize(hwnd);
    // Renderer初期化前にゲーム内モデルをCPUキャッシュへ揃える。
    auto preloadScene = std::make_unique<Scene_Game>(m_modelManager, m_input);
    if (!preloadScene->Initialize())
        return false;

    m_currentScene = std::make_unique<Scene_Title>(m_modelManager, m_input);
    if (!m_currentScene->Initialize())
        return false;
    if (!m_currentScene->InitializeUI(m_uiManager))
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

    m_input.Update();
    m_currentScene->Update(deltaTime);
    m_currentScene->Draw(m_renderer);
    m_uiManager.BeginFrame();
    m_currentScene->DrawUI(m_uiManager);
    m_renderer.Render(m_uiManager);

    const auto request = m_currentScene->ConsumeSceneRequest();
    if (request != SceneRequest::None)
    {
        auto nextScene = CreateRequestedScene(request, m_modelManager, m_input);
        if (nextScene->Initialize() && nextScene->InitializeUI(m_uiManager))
            m_currentScene = std::move(nextScene);
    }
}
