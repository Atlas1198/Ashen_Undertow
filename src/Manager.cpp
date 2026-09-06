#include "Manager.h"

#include "GameScene.h"

bool Manager::Initialize(HWND hwnd, uint32_t width, uint32_t height)
{
    if (!m_renderer.Initialize(hwnd, width, height))
        return false;

    m_currentScene = std::make_unique<GameScene>();
    m_currentScene->Initialize();
    return true;
}

void Manager::RunFrame()
{
    m_currentScene->Update();
    m_renderer.Render();
}
