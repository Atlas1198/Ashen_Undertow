#include "Player.h"
#include "Collider.h"
#include "AnimationComponent.h"
#include "Rigidbody.h"

#include <algorithm>
#include <cmath>
#include "Input.h"
#include "ModelComponent.h"
#include "GameAssets.h"
#include "Terrain.h"

namespace
{
constexpr float FootOffset = 1.05f;
constexpr float JumpSpeed = 3.0f;
constexpr float AttackPlaybackSpeed = 2.0f;
constexpr float AnimationBlendDuration = 0.2f;
constexpr float RotationResponsiveness = 12.0f;
constexpr float AttackColliderRadius = 0.7f;
constexpr float AttackColliderDistance = 0.9f;
constexpr float AttackColliderHeightOffset = -0.15f;
constexpr int AttackDamage = 100;
}

bool Player::Initialize(ModelManager& modelManager)
{
    InitializeHP(100);
    auto* model = AddComponent<ModelComponent>(modelManager, GameAssets::PlayerModel);
    if (!model) return false;
    model->SetOffset(0.0f, -1.05f, 0.0f);
    model->SetLocalRotation(0.0f, DirectX::XM_PI, 0.0f);
    m_animation = AddComponent<AnimationComponent>(modelManager, GameAssets::PlayerModel);
    if (!m_animation) return false;
    auto* collider = AddComponent<CapsuleCollider>(0.4f, 0.4f);
    if (!collider) return false;
    collider->SetOffset(0.0f, -0.3f, 0.0f);
    m_attackCollider = AddComponent<SphereCollider>(AttackColliderRadius);
    if (!m_attackCollider) return false;
    m_attackCollider->SetTrigger(true);
    m_attackCollider->SetEnabled(false);
    m_rigidbody = AddComponent<Rigidbody>();
    if (!m_rigidbody) return false;
    m_rigidbody->SetUseGravity(false);
    m_position.y = Terrain::HeightAt(m_position.x, m_position.z) + FootOffset;
    return true;
}

void Player::Update(float deltaTime)
{
    const float moveX = m_input.MoveX();
    const float moveZ = m_input.MoveZ();

    const float length = std::sqrt(moveX * moveX + moveZ * moveZ);
    const bool dashing = length > 0.0f && m_input.DashHeld();

    auto velocity = m_rigidbody->GetVelocity();
    const float groundHeight = Terrain::HeightAt(m_position.x, m_position.z) + FootOffset;
    const bool grounded = !m_rigidbody->UsesGravity() ||
        (m_position.y <= groundHeight && velocity.y <= 0.0f);
    if (grounded)
    {
        m_position.y = groundHeight;
        velocity.y = 0.0f;
        m_rigidbody->SetVelocity(velocity.x, velocity.y, velocity.z);
        m_rigidbody->SetUseGravity(false);
        m_jumping = false;
    }

    if (grounded && m_input.JumpPressed())
    {
        m_attacking = false;
        m_attackCollider->SetEnabled(false);
        m_jumping = m_animation->Play(
            "Jump", false, 0.0f, AnimationBlendDuration);
        m_rigidbody->SetUseGravity(true);
        m_rigidbody->SetVelocity(velocity.x, JumpSpeed, velocity.z);
    }

    if (m_jumping)
    {
        const float verticalVelocity = m_rigidbody->GetVelocity().y;
        const float jumpProgress = std::clamp(
            0.5f * (1.0f - verticalVelocity / JumpSpeed), 0.0f, 1.0f);
        m_animation->SetNormalizedTime(jumpProgress);
    }

    if (!m_jumping)
    {
        if (m_attacking && m_animation->IsFinished())
        {
            m_attacking = false;
            m_attackCollider->SetEnabled(false);
        }
        if (m_input.AttackPressed() && !m_attacking)
        {
            m_attacking = m_animation->Play(
                "Attack", false, AttackPlaybackSpeed, AnimationBlendDuration);
            if (m_attacking)
            {
                m_attackHit = false;
                m_attackCollider->SetEnabled(true);
            }
        }
        if (!m_attacking)
        {
            const bool blendFromCurrent = m_animation->IsPlaying("Idle") ||
                m_animation->IsPlaying("Walk") || m_animation->IsPlaying("Run") ||
                m_animation->IsPlaying("Attack") || m_animation->IsPlaying("Jump");
            const float blendDuration = blendFromCurrent ? AnimationBlendDuration : 0.0f;
            if (dashing)
                m_animation->Play("Run", true, 1.0f, blendDuration);
            else if (length > 0.0f)
                m_animation->Play("Walk", true, 1.0f, blendDuration);
            else
                m_animation->Play("Idle", true, 1.0f, blendDuration);
        }
    }
    if (length > 0.0f && !m_attacking)
    {
        const float moveSpeed = dashing ? 6.0f : 3.0f;
        const float localX = moveX / length;
        const float localZ = moveZ / length;
        const float sinYaw = std::sin(m_movementYaw);
        const float cosYaw = std::cos(m_movementYaw);
        const float worldX = localX * cosYaw + localZ * sinYaw;
        const float worldZ = -localX * sinYaw + localZ * cosYaw;
        const float targetYaw = std::atan2(worldX, worldZ);
        const float angleDifference = std::remainder(
            targetYaw - m_rotation.y, DirectX::XM_2PI);
        const float rotationAmount = 1.0f - std::exp(-RotationResponsiveness * deltaTime);
        m_rotation.y += angleDifference * rotationAmount;
        m_position.x += worldX * moveSpeed * deltaTime;
        m_position.z += worldZ * moveSpeed * deltaTime;
    }

    m_attackCollider->SetOffset(
        std::sin(m_rotation.y) * AttackColliderDistance,
        AttackColliderHeightOffset,
        std::cos(m_rotation.y) * AttackColliderDistance);
}

void Player::ResolveAttackHit(GameObject& target)
{
    if (!m_attackCollider->IsEnabled() || m_attackHit ||
        !m_attackCollider->HasContactWith(target))
    {
        return;
    }
    target.TakeDamage(AttackDamage);
    m_attackHit = true;
}

void Player::ResolveTerrainContact()
{
    // RigidbodyとObject同士の衝突解決後、最終位置を地面へ合わせる。
    Terrain::ClampToArena(m_position.x, m_position.z);
    const float ground = Terrain::HeightAt(m_position.x, m_position.z) + FootOffset;
    const auto velocity = m_rigidbody->GetVelocity();
    if (!m_rigidbody->UsesGravity() || (m_position.y <= ground && velocity.y <= 0.0f))
    {
        m_position.y = ground;
        m_rigidbody->SetVelocity(velocity.x, 0.0f, velocity.z);
        m_rigidbody->SetUseGravity(false);
        m_jumping = false;
    }
}

ModelHandle Player::GetModelHandle() const
{
    const auto* model = GetComponent<ModelComponent>();
    return model ? model->GetModelHandle() : ModelHandle {};
}
