#include "Scene_Game.h"

#include "Enemy.h"
#include "Player.h"
#include "Input.h"

#include "Renderer.h"

Scene_Game::Scene_Game(ModelManager& modelManager, const Input& input)
    : SceneManager(modelManager), m_input(input)
{
}

bool Scene_Game::Initialize()
{
    m_player = AddGameObject<Player>(m_input);
    if (!m_player) return false;

    auto* enemy = AddGameObject<Enemy>();
    if (!enemy) return false;
    enemy->SetPosition(3.0f, 0.0f, 3.0f);
    return true;
}

void Scene_Game::OnUpdate(float)
{
    m_camera.Rotate(m_input.LookX(), m_input.LookY());
}

void Scene_Game::OnAfterUpdate(float)
{
    m_camera.SetTarget(m_player->GetPosition());
}

void Scene_Game::OnDraw(Renderer& renderer)
{
    renderer.SetCamera(m_camera.GetPosition(), m_camera.GetTarget());
}
