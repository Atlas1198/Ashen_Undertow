#include "Player.h"
#include "CapsuleCollider.h"
#include "AnimationComponent.h"
#include "Rigidbody.h"

#include <cmath>
#include "Input.h"
#include "ModelComponent.h"
#include "GameAssets.h"

namespace
{
constexpr float GroundHeight = 0.0f;
constexpr float JumpSpeed = 3.0f;
}

bool Player::Initialize(ModelManager& modelManager)
{
    auto* model = AddComponent<ModelComponent>(modelManager, GameAssets::PlayerModel);
    if (!model) return false;
    model->SetOffset(0.0f, -1.05f, 0.0f);
    m_animation = AddComponent<AnimationComponent>(modelManager, GameAssets::PlayerModel);
    if (!m_animation) return false;
    if (!AddComponent<CapsuleCollider>(0.5f, 0.5f)) return false;
    m_rigidbody = AddComponent<Rigidbody>();
    if (!m_rigidbody) return false;
    m_rigidbody->SetUseGravity(false);
    return true;
}

void Player::Update(float deltaTime)
{
    const float moveX = m_input.MoveX();
    const float moveZ = m_input.MoveZ();

    const float length = std::sqrt(moveX * moveX + moveZ * moveZ);
    const bool dashing = length > 0.0f && m_input.DashHeld();

    auto velocity = m_rigidbody->GetVelocity();
    const bool grounded = m_position.y <= GroundHeight && velocity.y <= 0.0f;
    if (grounded)
    {
        m_position.y = GroundHeight;
        velocity.y = 0.0f;
        m_rigidbody->SetVelocity(velocity.x, velocity.y, velocity.z);
        m_rigidbody->SetUseGravity(false);
        m_jumping = false;
    }

    if (grounded && m_input.JumpPressed())
    {
        m_attacking = false;
        m_jumping = m_animation->Play("Jump", false);
        m_rigidbody->SetUseGravity(true);
        m_rigidbody->SetVelocity(velocity.x, JumpSpeed, velocity.z);
    }

    if (!m_jumping)
    {
        if (m_attacking && m_animation->IsFinished())
            m_attacking = false;
        if (m_input.AttackPressed() && !m_attacking)
            m_attacking = m_animation->Play("Attack", false);
        if (!m_attacking)
            m_animation->Play(length > 0.0f ? (dashing ? "Run" : "Walk") : "Idle");
    }
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
