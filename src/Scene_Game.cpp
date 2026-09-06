#include "Scene_Game.h"

#include "Renderer.h"

GameScene::GameScene(ModelManager& modelManager)
    : m_modelManager(modelManager)
{
}

bool GameScene::Initialize()
{
    m_player = std::make_unique<Player>();
    return m_player->Initialize(m_modelManager);
}

void GameScene::Update(float deltaTime)
{
    m_player->Update(deltaTime);
}

void GameScene::Draw(Renderer& renderer)
{
    renderer.SetCameraTarget(m_player->GetPosition());
    m_player->Draw(renderer);
}
