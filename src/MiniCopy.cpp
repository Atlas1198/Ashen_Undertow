#include "MiniCopy.h"

#include "AnimationComponent.h"
#include "Boss.h"
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
constexpr float RunEnterDistance = 5.5f;
constexpr float RunExitDistance = 4.5f;
constexpr float AttackDistance = 1.8f;
constexpr float WalkSpeed = 1.5f;
constexpr float RunSpeed = 3.0f;
constexpr float RotationResponsiveness = 8.0f;
constexpr float AnimationBlendDuration = 0.2f;
constexpr float AttackPlaybackSpeed = 2.0f;
constexpr float AttackCooldown = 0.5f;
constexpr float AttackColliderRadius = 0.32f;
constexpr float AttackHitStart = 0.25f;
constexpr float AttackHitEnd = 0.70f;
constexpr int AttackDamage = 3;
constexpr float BlockKnockbackDistance = 1.2f;
constexpr int BossDamageOnDefeat = 100;
}

bool MiniCopy::Initialize(ModelManager& modelManager)
{
    InitializeHP(300);
    auto* model = AddComponent<ModelComponent>(modelManager, GameAssets::EnemyModel);
    if (!model) return false;
    model->SetLocalRotation(0.0f, DirectX::XM_PI, 0.0f);
    m_animation = AddComponent<AnimationComponent>(modelManager, GameAssets::EnemyModel);
    if (!m_animation) return false;
    m_attackCollider = AddComponent<SegmentCollider>(AttackColliderRadius);
    if (!m_attackCollider) return false;
    m_attackCollider->SetTrigger(true);
    m_attackCollider->SetRole(ColliderRole::Hitbox);
    m_attackCollider->SetEnabled(false);
    if (!AddComponent<BoneSegmentColliderAttachment>(*m_animation, *model,
        *static_cast<SegmentCollider*>(m_attackCollider),
        "mixamorig:RightForeArm", "mixamorig:RightHand")) return false;
    m_leftAttackCollider = AddComponent<SegmentCollider>(0.24f);
    if (!m_leftAttackCollider) return false;
    m_leftAttackCollider->SetTrigger(true);
    m_leftAttackCollider->SetRole(ColliderRole::Hitbox);
    m_leftAttackCollider->SetEnabled(false);
    if (!AddComponent<BoneSegmentColliderAttachment>(*m_animation, *model,
        *static_cast<SegmentCollider*>(m_leftAttackCollider),
        "mixamorig:LeftHand", "mixamorig:LeftHand",
        DirectX::XMFLOAT3 { -10.2f, 6.0f, 0.7f },
        DirectX::XMFLOAT3 { -34.7f, 68.6f, -14.4f })) return false;
    const auto addHurtbox = [&](float radius, const char* start, const char* end,
        bool trigger = true)
    {
        auto* hurtbox = AddComponent<SegmentCollider>(radius);
        if (!hurtbox) return false;
        hurtbox->SetTrigger(trigger);
        hurtbox->SetRole(ColliderRole::Hurtbox);
        if (!AddComponent<BoneSegmentColliderAttachment>(*m_animation, *model,
            *hurtbox, start, end)) return false;
        m_hurtboxes.push_back(hurtbox);
        return true;
    };
    if (!addHurtbox(0.40f, "mixamorig:Hips", "mixamorig:Spine2", false) ||
        !addHurtbox(0.23f, "mixamorig:LeftArm", "mixamorig:LeftHand") ||
        !addHurtbox(0.23f, "mixamorig:RightArm", "mixamorig:RightHand") ||
        !addHurtbox(0.26f, "mixamorig:LeftUpLeg", "mixamorig:LeftFoot") ||
        !addHurtbox(0.26f, "mixamorig:RightUpLeg", "mixamorig:RightFoot")) return false;
    auto* head = AddComponent<SphereCollider>(0.32f);
    if (!head) return false;
    head->SetTrigger(true);
    head->SetRole(ColliderRole::Hurtbox);
    if (!AddComponent<BoneColliderAttachment>(*m_animation, *model,
        *head, "mixamorig:Head")) return false;
    m_headCollider = head;
    m_hurtboxes.push_back(head);
    return true;
}

void MiniCopy::Update(float deltaTime)
{
    m_attackCooldown = std::max(0.0f, m_attackCooldown - deltaTime);
    if (IsDead())
    {
        m_attacking = false;
        m_attackCollider->SetEnabled(false);
        m_leftAttackCollider->SetEnabled(false);
        for (auto* hurtbox : m_hurtboxes) hurtbox->SetEnabled(false);
        if (!m_deathReported)
        {
            m_boss.TakeDamage(BossDamageOnDefeat);
            m_deathReported = true;
            m_scale = { 0.0f, 0.0f, 0.0f };
        }
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
        m_leftAttackCollider->SetEnabled(false);
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
                m_leftAttackCollider->SetEnabled(false);
            }
        }
        else if (!m_attacking && distance > AttackDistance)
        {
            if (m_running)
                m_running = distance > RunExitDistance;
            else
                m_running = distance > RunEnterDistance;
            const float speed = m_running ? RunSpeed : WalkSpeed;
            m_animation->Play(m_running ? "Run" : "Walk",
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
        m_leftAttackCollider->SetEnabled(
            progress >= AttackHitStart && progress <= AttackHitEnd);
    }
}

void MiniCopy::ResolveAttackHit(GameObject& target)
{
    const bool rightHit = m_attackCollider->IsEnabled() &&
        m_attackCollider->HasContactWith(target, ColliderRole::Hurtbox);
    const bool leftHit = m_leftAttackCollider->IsEnabled() &&
        m_leftAttackCollider->HasContactWith(target, ColliderRole::Hurtbox);
    if (m_attackHit || (!rightHit && !leftHit))
    {
        return;
    }
    if (auto* player = dynamic_cast<Player*>(&target);
        player && player->CanBlockAttackFrom(m_position))
    {
        player->ConsumeBlockStamina();
        const auto playerPosition = player->GetPosition();
        float awayX = m_position.x - playerPosition.x;
        float awayZ = m_position.z - playerPosition.z;
        const float distance = std::sqrt(awayX * awayX + awayZ * awayZ);
        if (distance > 0.001f)
        {
            awayX /= distance;
            awayZ /= distance;
        }
        else
        {
            awayX = -std::sin(player->GetRotation().y);
            awayZ = -std::cos(player->GetRotation().y);
        }
        m_position.x += awayX * BlockKnockbackDistance;
        m_position.z += awayZ * BlockKnockbackDistance;
        Terrain::ClampToArena(m_position.x, m_position.z);
        m_position.y = Terrain::HeightAt(m_position.x, m_position.z);
        player->ApplyBlockKnockback(m_position);
        m_attacking = false;
        m_attackCollider->SetEnabled(false);
        m_leftAttackCollider->SetEnabled(false);
        m_attackCooldown = AttackCooldown;
        m_animation->Play("Idle", true, 1.0f, AnimationBlendDuration);
        m_attackHit = true;
        return;
    }
    target.TakeDamage(AttackDamage);
    m_attackHit = true;
}

void MiniCopy::Respawn(const DirectX::XMFLOAT3& position)
{
    Heal(GetMaxHP());
    m_position = position;
    m_scale = { 1.0f, 1.0f, 1.0f };
    m_attackCooldown = 0.0f;
    m_inCombat = true;
    m_attacking = false;
    m_attackHit = false;
    m_running = false;
    m_deathReported = false;
    m_attackCollider->SetEnabled(false);
    m_leftAttackCollider->SetEnabled(false);
    for (auto* hurtbox : m_hurtboxes) hurtbox->SetEnabled(true);
    m_animation->Play("Idle", true, 1.0f, 0.0f);
}

DirectX::XMFLOAT3 MiniCopy::GetHPBarPosition() const
{
    const auto shape = m_headCollider->GetShape();
    auto position = shape.center;
    position.y += shape.radius + 0.12f;
    return position;
}
