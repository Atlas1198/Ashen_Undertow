#include "Scene_Game.h"

#include "Enemy.h"
#include "Player.h"
#include "CapsuleCollider.h"
#include "SphereCollider.h"
#include "Input.h"

#include <algorithm>
#include <cmath>
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
    m_enemy->SetPosition(3.0f, 0.0f, 3.0f);
    return true;
}

void Scene_Game::OnUpdate(float)
{
    m_camera.Rotate(m_input.LookX(), m_input.LookY());
}

void Scene_Game::OnAfterUpdate(float)
{
    const auto* playerCollider = m_player->GetComponent<CapsuleCollider>();
    const auto* enemyCollider = m_enemy->GetComponent<SphereCollider>();
    for (const auto* contact : playerCollider->GetContacts())
    {
        if (&contact->GetOwner() != m_enemy) continue;

        const auto playerShape = playerCollider->GetShape();
        const auto enemyShape = enemyCollider->GetShape();
        const float closestY = std::clamp(
            enemyShape.center.y,
            playerShape.center.y - playerShape.halfExtents.y,
            playerShape.center.y + playerShape.halfExtents.y);
        float pushX = playerShape.center.x - enemyShape.center.x;
        float pushY = closestY - enemyShape.center.y;
        float pushZ = playerShape.center.z - enemyShape.center.z;
        const float distance = std::sqrt(pushX * pushX + pushY * pushY + pushZ * pushZ);
        const float penetration = playerShape.radius + enemyShape.radius - distance;
        if (penetration <= 0.0f) break;

        if (distance > 0.0001f)
        {
            pushX /= distance;
            pushY /= distance;
            pushZ /= distance;
        }
        else
        {
            pushX = 1.0f;
            pushY = 0.0f;
            pushZ = 0.0f;
        }

        const auto position = m_player->GetPosition();
        m_player->SetPosition(
            position.x + pushX * penetration,
            position.y + pushY * penetration,
            position.z + pushZ * penetration);
        break;
    }

    m_camera.SetTarget(m_player->GetPosition());
}

void Scene_Game::OnDraw(Renderer& renderer)
{
    renderer.SetCamera(m_camera.GetPosition(), m_camera.GetTarget());
}
