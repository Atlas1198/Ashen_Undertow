#include "Scene_Game.h"

#include "Renderer.h"

GameScene::GameScene(ModelManager& modelManager)
    : SceneManager(modelManager)
{
}

bool GameScene::Initialize()
{
    m_player = AddGameObject<Player>();
    return m_player != nullptr;
}

void GameScene::OnDraw(Renderer& renderer)
{
    renderer.SetCameraTarget(m_player->GetPosition());
}
