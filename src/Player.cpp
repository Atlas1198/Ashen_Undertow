#include "Player.h"
#include "CapsuleCollider.h"
#include "AnimationComponent.h"

#include <cmath>
#include "Input.h"
#include "ModelComponent.h"
#include "GameAssets.h"

bool Player::Initialize(ModelManager& modelManager)
{
    auto* model = AddComponent<ModelComponent>(modelManager, GameAssets::PlayerModel);
    if (!model) return false;
    model->SetOffset(0.0f, -1.05f, 0.0f);
    m_animation = AddComponent<AnimationComponent>(modelManager, GameAssets::PlayerModel);
    if (!m_animation) return false;
    return AddComponent<CapsuleCollider>(0.5f, 0.5f) != nullptr;
}

void Player::Update(float deltaTime)
{
    const float moveX = m_input.MoveX();
    const float moveZ = m_input.MoveZ();

    const float length = std::sqrt(moveX * moveX + moveZ * moveZ);
    const bool dashing = length > 0.0f && m_input.DashHeld();
    if (m_attacking && m_animation->IsFinished())
        m_attacking = false;
    if (m_input.AttackPressed() && !m_attacking)
        m_attacking = m_animation->Play("Attack", false);
    if (!m_attacking)
        m_animation->Play(length > 0.0f ? (dashing ? "Run" : "Walk") : "Idle");
    if (length > 0.0f)
    {
        const float moveSpeed = dashing ? 6.0f : 3.0f;
        const float localX = moveX / length;
        const float localZ = moveZ / length;
        const float sinYaw = std::sin(m_rotation.y);
        const float cosYaw = std::cos(m_rotation.y);
        const float worldX = localX * cosYaw + localZ * sinYaw;
        const float worldZ = -localX * sinYaw + localZ * cosYaw;
        m_position.x += worldX * moveSpeed * deltaTime;
        m_position.z += worldZ * moveSpeed * deltaTime;
    }
}

ModelHandle Player::GetModelHandle() const
{
    const auto* model = GetComponent<ModelComponent>();
    return model ? model->GetModelHandle() : ModelHandle {};
}
