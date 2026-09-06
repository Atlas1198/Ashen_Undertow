#include "Scene_Game.h"

#include "Enemy.h"

#include "Renderer.h"

GameScene::GameScene(ModelManager& modelManager)
    : SceneManager(modelManager)
{
}

bool GameScene::Initialize()
{
    m_player = AddGameObject<Player>();
    if (!m_player) return false;

    auto* enemy = AddGameObject<Enemy>();
    if (!enemy) return false;
    enemy->SetPosition(3.0f, 0.0f, 3.0f);
    return true;
}

void GameScene::OnDraw(Renderer& renderer)
{
    renderer.SetCameraTarget(m_player->GetPosition());
}
