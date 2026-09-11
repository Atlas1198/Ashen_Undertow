#include "Enemy.h"

#include "AnimationComponent.h"
#include "Collider.h"
#include "GameAssets.h"
#include "ModelComponent.h"
#include "Player.h"
#include "Terrain.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr float SightRange = 10.0f;
constexpr float RunStartDistance = 5.0f;
constexpr float AttackDistance = 1.8f;
constexpr float WalkSpeed = 1.5f;
constexpr float RunSpeed = 3.0f;
constexpr float RotationResponsiveness = 8.0f;
constexpr float AnimationBlendDuration = 0.2f;
constexpr float AttackPlaybackSpeed = 2.0f;
constexpr float AttackCooldown = 0.5f;
constexpr float AttackColliderRadius = 0.8f;
constexpr float AttackHitStart = 0.25f;
constexpr float AttackHitEnd = 0.70f;
constexpr int AttackDamage = 10;
}

bool Enemy::Initialize(ModelManager& modelManager)
{
    InitializeHP(1000);
    auto* model = AddComponent<ModelComponent>(modelManager, GameAssets::EnemyModel);
    if (!model) return false;
    model->SetLocalRotation(0.0f, DirectX::XM_PI, 0.0f);
    m_animation = AddComponent<AnimationComponent>(modelManager, GameAssets::EnemyModel);
    if (!m_animation) return false;
    auto* collider = AddComponent<CapsuleCollider>(0.4f, 0.4f);
    if (!collider) return false;
    collider->SetOffset(0.0f, 0.8f, 0.0f);
    m_attackCollider = AddComponent<SphereCollider>(AttackColliderRadius);
    if (!m_attackCollider) return false;
    m_attackCollider->SetTrigger(true);
    m_attackCollider->SetRole(ColliderRole::Hitbox);
    m_attackCollider->SetEnabled(false);
    if (!AddComponent<BoneColliderAttachment>(*m_animation, *model,
        *m_attackCollider, "mixamorig:RightHand")) return false;
    auto* chest = AddComponent<SphereCollider>(0.55f);
    if (!chest) return false;
    chest->SetTrigger(true);
    chest->SetRole(ColliderRole::Hurtbox);
    if (!AddComponent<BoneColliderAttachment>(*m_animation, *model,
        *chest, "mixamorig:Spine2")) return false;
    auto* head = AddComponent<SphereCollider>(0.32f);
    if (!head) return false;
    head->SetTrigger(true);
    head->SetRole(ColliderRole::Hurtbox);
    if (!AddComponent<BoneColliderAttachment>(*m_animation, *model,
        *head, "mixamorig:Head")) return false;
    return true;
}

void Enemy::Update(float deltaTime)
{
    m_attackCooldown = std::max(0.0f, m_attackCooldown - deltaTime);
    if (IsDead())
    {
        m_attacking = false;
        m_attackCollider->SetEnabled(false);
        m_animation->Play("Idle", true, 1.0f, AnimationBlendDuration);
        return;
    }

    const auto targetPosition = m_target.GetPosition();
    const float differenceX = targetPosition.x - m_position.x;
    const float differenceZ = targetPosition.z - m_position.z;
    const float distance = std::sqrt(
        differenceX * differenceX + differenceZ * differenceZ);
    if (!m_inCombat && (distance <= SightRange || m_target.IsAttacking() ||
        GetHP() < GetMaxHP()))
        m_inCombat = true;

    if (!m_inCombat)
    {
        m_animation->Play("Idle");
        return;
    }

    if (m_attacking && m_animation->IsFinished())
    {
        m_attacking = false;
        m_attackCollider->SetEnabled(false);
        m_attackCooldown = AttackCooldown;
    }

    if (distance > 0.001f)
    {
        const float directionX = differenceX / distance;
        const float directionZ = differenceZ / distance;
        const float targetYaw = std::atan2(directionX, directionZ);
        const float angleDifference = std::remainder(
            targetYaw - m_rotation.y, DirectX::XM_2PI);
        const float rotationAmount = 1.0f -
            std::exp(-RotationResponsiveness * deltaTime);
        m_rotation.y += angleDifference * rotationAmount;

        if (!m_attacking && distance <= AttackDistance && m_attackCooldown <= 0.0f)
        {
            m_attacking = m_animation->Play(
                "Attack", false, AttackPlaybackSpeed, AnimationBlendDuration);
            if (m_attacking)
            {
                m_attackHit = false;
                m_attackCollider->SetEnabled(false);
            }
        }
        else if (!m_attacking && distance > AttackDistance)
        {
            const bool running = distance > RunStartDistance;
            const float speed = running ? RunSpeed : WalkSpeed;
            m_animation->Play(running ? "Run" : "Walk",
                true, 1.0f, AnimationBlendDuration);
            m_position.x += directionX * speed * deltaTime;
            m_position.z += directionZ * speed * deltaTime;
            Terrain::ClampToArena(m_position.x, m_position.z);
            m_position.y = Terrain::HeightAt(m_position.x, m_position.z);
        }
        else if (!m_attacking)
        {
            m_animation->Play("Idle", true, 1.0f, AnimationBlendDuration);
        }
    }

    if (m_attacking)
    {
        const float progress = m_animation->GetNormalizedTime();
        m_attackCollider->SetEnabled(
            progress >= AttackHitStart && progress <= AttackHitEnd);
    }
}

void Enemy::ResolveAttackHit(GameObject& target)
{
    if (!m_attackCollider->IsEnabled() || m_attackHit ||
        !m_attackCollider->HasContactWith(target, ColliderRole::Hurtbox))
    {
        return;
    }
    target.TakeDamage(AttackDamage);
    m_attackHit = true;
}
