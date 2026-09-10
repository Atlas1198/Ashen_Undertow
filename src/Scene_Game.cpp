#include "Scene_Game.h"

#include "Enemy.h"
#include "Player.h"
#include "Tree.h"
#include "Collider.h"
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

    m_enemy = AddGameObject<Enemy>();
    if (!m_enemy) return false;
    m_enemy->SetPosition(3.0f, -1.05f, 3.0f);

    m_tree = AddGameObject<Tree>();
    if (!m_tree) return false;
    m_tree->SetPosition(-3.0f, -1.05f, 3.0f);
    return true;
}

void Scene_Game::OnUpdate(float)
{
    if (m_input.ColliderVisualizationPressed())
        m_showColliders = !m_showColliders;
    m_camera.Rotate(m_input.LookX(), m_input.LookY());
    m_player->SetRotation(0.0f, -m_camera.GetYaw(), 0.0f);
}

void Scene_Game::OnAfterUpdate(float)
{
    m_camera.SetTarget(m_player->GetPosition());
    m_tree->FaceCamera(m_camera.GetPosition());
}

void Scene_Game::OnDraw(Renderer& renderer)
{
    renderer.SetCamera(m_camera.GetPosition(), m_camera.GetTarget());
    if (!m_showColliders) return;

    const auto* playerCollider = m_player->GetComponent<CapsuleCollider>();
    const auto* enemyCollider = m_enemy->GetComponent<CapsuleCollider>();
    const auto playerShape = playerCollider->GetShape();
    const auto enemyShape = enemyCollider->GetShape();
    renderer.DrawCollider(playerShape.center, playerShape.halfExtents,
        playerShape.radius, playerCollider->IsColliding());
    renderer.DrawCollider(enemyShape.center, enemyShape.halfExtents,
        enemyShape.radius, enemyCollider->IsColliding());
}
