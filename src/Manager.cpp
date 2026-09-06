#include "Manager.h"

#include "Scene_Game.h"

bool Manager::Initialize(HWND hwnd, uint32_t width, uint32_t height)
{
    m_currentScene = std::make_unique<GameScene>(m_modelManager);
    if (!m_currentScene->Initialize())
        return false;

    return m_renderer.Initialize(hwnd, width, height, m_modelManager);
}

void Manager::RunFrame()
{
    m_currentScene->Update();
    m_currentScene->Draw(m_renderer);
    m_renderer.Render();
}
