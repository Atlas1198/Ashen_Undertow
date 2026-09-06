#include "Scene_Game.h"

GameScene::GameScene(ModelManager& modelManager)
    : m_modelManager(modelManager)
{
}

bool GameScene::Initialize()
{
    m_player = std::make_unique<Player>();
    return m_player->Initialize(m_modelManager);
}

void GameScene::Update()
{
    m_player->Update();
}

void GameScene::Draw(Renderer& renderer)
{
    m_player->Draw(renderer);
}
