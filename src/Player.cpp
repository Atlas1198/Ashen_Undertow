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
constexpr float AttackColliderRadius = 0.11f;
constexpr float AttackHitStart = 0.22f;
constexpr float AttackHitEnd = 0.68f;
constexpr float EvasionColliderRadius = 0.5f;
constexpr float EvasionColliderHeightOffset = -0.55f;
constexpr float EvasionPlaybackSpeed = 2.0f;
constexpr float EvasionSpeed = 3.0f;
constexpr float EvasionVisualYawOffset = DirectX::XM_PIDIV2;
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
    m_evasionCollider = AddComponent<SphereCollider>(EvasionColliderRadius);
    if (!m_evasionCollider) return false;
    m_evasionCollider->SetOffset(0.0f, EvasionColliderHeightOffset, 0.0f);
    m_evasionCollider->SetRole(ColliderRole::Hurtbox);
    m_evasionCollider->SetEnabled(false);
    m_attackCollider = AddComponent<SegmentCollider>(AttackColliderRadius);
    if (!m_attackCollider) return false;
    m_attackCollider->SetTrigger(true);
    m_attackCollider->SetRole(ColliderRole::Hitbox);
    m_attackCollider->SetEnabled(false);
    if (!AddComponent<BoneSegmentColliderAttachment>(*m_animation, *model,
        *static_cast<SegmentCollider*>(m_attackCollider),
        "mixamorig:RightHand", "mixamorig:RightHand",
        DirectX::XMFLOAT3 { 1.3f, -0.2f, -1.4f },
        DirectX::XMFLOAT3 { -39.2f, 97.5f, -56.5f })) return false;
    const auto addHurtbox = [&](float radius, const char* start, const char* end,
        bool trigger = true)
    {
        auto* hurtbox = AddComponent<SegmentCollider>(radius);
        if (!hurtbox) return false;
        hurtbox->SetTrigger(trigger);
        hurtbox->SetRole(ColliderRole::Hurtbox);
        if (!AddComponent<BoneSegmentColliderAttachment>(*m_animation, *model,
            *hurtbox, start, end)) return false;
        m_standingHurtboxes.push_back(hurtbox);
        return true;
    };
    if (!addHurtbox(0.24f, "mixamorig:Hips", "mixamorig:Spine2", false) ||
        !addHurtbox(0.12f, "mixamorig:LeftArm", "mixamorig:LeftHand") ||
        !addHurtbox(0.12f, "mixamorig:RightArm", "mixamorig:RightHand") ||
        !addHurtbox(0.15f, "mixamorig:LeftUpLeg", "mixamorig:LeftFoot") ||
        !addHurtbox(0.15f, "mixamorig:RightUpLeg", "mixamorig:RightFoot")) return false;
    auto* head = AddComponent<SphereCollider>(0.25f);
    if (!head) return false;
    head->SetTrigger(true);
    head->SetRole(ColliderRole::Hurtbox);
    if (!AddComponent<BoneColliderAttachment>(*m_animation, *model,
        *head, "mixamorig:Head")) return false;
    m_standingHurtboxes.push_back(head);
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
    float movementX = 0.0f;
    float movementZ = 0.0f;
    if (length > 0.0f)
    {
        const float localX = moveX / length;
        const float localZ = moveZ / length;
        const float sinYaw = std::sin(m_movementYaw);
        const float cosYaw = std::cos(m_movementYaw);
        movementX = localX * cosYaw + localZ * sinYaw;
        movementZ = -localX * sinYaw + localZ * cosYaw;
    }

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

    if (m_evading && m_animation->IsFinished())
    {
        m_evading = false;
        m_evasionCollider->SetEnabled(false);
        for (auto* hurtbox : m_standingHurtboxes) hurtbox->SetEnabled(true);
    }

    if (!m_evading && grounded && m_input.JumpPressed())
    {
        m_attacking = false;
        m_attackCollider->SetEnabled(false);
        if (dashing)
        {
            m_evading = m_animation->Play(
                "Evasion", false, EvasionPlaybackSpeed, AnimationBlendDuration);
            if (m_evading)
            {
                m_evasionDirectionX = movementX;
                m_evasionDirectionZ = movementZ;
                m_rotation.y = std::atan2(
                    m_evasionDirectionX, m_evasionDirectionZ) + EvasionVisualYawOffset;
                m_evasionCollider->SetEnabled(true);
                for (auto* hurtbox : m_standingHurtboxes) hurtbox->SetEnabled(false);
            }
        }
        else
        {
            m_jumping = m_animation->Play(
                "Jump", false, 0.0f, AnimationBlendDuration);
            m_rigidbody->SetUseGravity(true);
            m_rigidbody->SetVelocity(velocity.x, JumpSpeed, velocity.z);
        }
    }

    if (m_jumping)
    {
        const float verticalVelocity = m_rigidbody->GetVelocity().y;
        const float jumpProgress = std::clamp(
            0.5f * (1.0f - verticalVelocity / JumpSpeed), 0.0f, 1.0f);
        m_animation->SetNormalizedTime(jumpProgress);
    }

    if (!m_jumping && !m_evading)
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
                m_attackCollider->SetEnabled(false);
            }
        }
        if (m_attacking)
        {
            const float progress = m_animation->GetNormalizedTime();
            m_attackCollider->SetEnabled(
                progress >= AttackHitStart && progress <= AttackHitEnd);
        }
        if (!m_attacking)
        {
            const bool blendFromCurrent = m_animation->IsPlaying("Idle") ||
                m_animation->IsPlaying("Walk") || m_animation->IsPlaying("Run") ||
                m_animation->IsPlaying("Attack") || m_animation->IsPlaying("Jump") ||
                m_animation->IsPlaying("Evasion");
            const float blendDuration = blendFromCurrent ? AnimationBlendDuration : 0.0f;
            if (dashing)
                m_animation->Play("Run", true, 1.0f, blendDuration);
            else if (length > 0.0f)
                m_animation->Play("Walk", true, 1.0f, blendDuration);
            else
                m_animation->Play("Idle", true, 1.0f, blendDuration);
        }
    }
    const auto rotateTowards = [&](float directionX, float directionZ)
    {
        const float targetYaw = std::atan2(directionX, directionZ);
        const float angleDifference = std::remainder(
            targetYaw - m_rotation.y, DirectX::XM_2PI);
        const float rotationAmount = 1.0f - std::exp(-RotationResponsiveness * deltaTime);
        m_rotation.y += angleDifference * rotationAmount;
    };

    if (m_evading)
    {
        m_position.x += m_evasionDirectionX * EvasionSpeed * deltaTime;
        m_position.z += m_evasionDirectionZ * EvasionSpeed * deltaTime;
    }
    else if (length > 0.0f && !m_attacking)
    {
        rotateTowards(movementX, movementZ);
        const float moveSpeed = dashing ? 6.0f : 3.0f;
        m_position.x += movementX * moveSpeed * deltaTime;
        m_position.z += movementZ * moveSpeed * deltaTime;
    }

}

void Player::ResolveAttackHit(GameObject& target)
{
    if (!m_attackCollider->IsEnabled() || m_attackHit ||
        !m_attackCollider->HasContactWith(target, ColliderRole::Hurtbox))
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
