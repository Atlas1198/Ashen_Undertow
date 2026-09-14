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
constexpr float ChargeAttackColliderRadius = 0.12f;
constexpr float AttackHitStart = 0.22f;
constexpr float AttackHitEnd = 0.68f;
constexpr float AttackComboTransition = 0.72f;
constexpr float AttackComboBlendDuration = 0.1f;
constexpr const char* AttackAnimations[] = { "Attack", "Attack2", "Attack3" };
constexpr int AttackComboCount = 3;
constexpr float ChargeActivationTime = 0.25f;
constexpr float ChargeLevelOneTime = 1.0f;
constexpr float ChargeLevelTwoTime = 2.0f;
constexpr float ChargeLevelThreeTime = 3.0f;
constexpr float ChargeHoldPose = 0.30f;
constexpr float ChargeHitStart = 0.38f;
constexpr float ChargeHitEnd = 0.56f;
constexpr float ChargeAttackPlaybackSpeed = 1.5f;
constexpr float ChargeStaminaCost = 30.0f;
constexpr float EvasionColliderRadius = 0.5f;
constexpr float EvasionColliderHeightOffset = -0.55f;
constexpr float EvasionPlaybackSpeed = 2.0f;
constexpr float EvasionSpeed = 3.0f;
constexpr float EvasionVisualYawOffset = DirectX::XM_PIDIV2;
constexpr float BlockPlaybackSpeed = 1.0f;
constexpr float BlockFacingThreshold = 0.5f;
constexpr float BlockKnockbackDistance = 0.3f;
constexpr float DashStaminaPerSecond = 100.0f / 30.0f;
constexpr float EvasionStaminaCost = 30.0f;
constexpr float BlockStaminaCost = 10.0f;
constexpr float JumpStaminaCost = 3.0f;
constexpr float StaminaRecoveryDelay = 2.0f;
constexpr float StaminaRecoveryPerSecond = 15.0f;
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
    m_chargeAttackCollider = AddComponent<SegmentCollider>(ChargeAttackColliderRadius);
    if (!m_chargeAttackCollider) return false;
    m_chargeAttackCollider->SetTrigger(true);
    m_chargeAttackCollider->SetRole(ColliderRole::Hitbox);
    m_chargeAttackCollider->SetEnabled(false);
    if (!AddComponent<BoneSegmentColliderAttachment>(*m_animation, *model,
        *static_cast<SegmentCollider*>(m_chargeAttackCollider),
        "mixamorig:Sword_joint", "mixamorig:Sword_joint",
        DirectX::XMFLOAT3 { 5.247f, 1.044f, 20.344f },
        DirectX::XMFLOAT3 { -20.701f, -5.250f, -81.074f })) return false;
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
    const bool dashRequested = length > 0.0f && m_input.DashHeld();
    const bool dashing = dashRequested && m_stamina > 0.0f;
    bool staminaConsumed = false;
    bool dashActive = false;
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

    const bool wasBlocking = m_blocking;
    m_blocking = grounded && !m_jumping && !m_evading && !IsAttacking()
        && m_input.BlockHeld() && m_stamina >= BlockStaminaCost;

    if (!m_evading && !m_blocking && !IsAttacking() &&
        grounded && m_input.JumpPressed())
    {
        m_attacking = false;
        m_attackComboStep = 0;
        m_attackComboQueued = false;
        m_attackCollider->SetEnabled(false);
        m_chargeAttackCollider->SetEnabled(false);
        if (dashRequested)
        {
            if (m_stamina >= EvasionStaminaCost)
            {
                m_evading = m_animation->Play(
                    "Evasion", false, EvasionPlaybackSpeed, AnimationBlendDuration);
                if (m_evading)
                {
                    m_stamina -= EvasionStaminaCost;
                    staminaConsumed = true;
                    m_evasionDirectionX = movementX;
                    m_evasionDirectionZ = movementZ;
                    m_rotation.y = std::atan2(
                        m_evasionDirectionX, m_evasionDirectionZ) + EvasionVisualYawOffset;
                    m_evasionCollider->SetEnabled(true);
                    for (auto* hurtbox : m_standingHurtboxes) hurtbox->SetEnabled(false);
                }
            }
        }
        else if (m_stamina >= JumpStaminaCost)
        {
            m_jumping = m_animation->Play(
                "Jump", false, 0.0f, AnimationBlendDuration);
            if (m_jumping)
            {
                m_stamina -= JumpStaminaCost;
                staminaConsumed = true;
                m_rigidbody->SetUseGravity(true);
                m_rigidbody->SetVelocity(velocity.x, JumpSpeed, velocity.z);
            }
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
        const bool attackPressed = m_input.AttackPressed();
        const bool attackHeld = m_input.AttackHeld();
        const bool attackReleased = m_input.AttackReleased();
        bool normalAttackInput = m_attacking && attackReleased;

        if (attackPressed && !IsAttacking() && !m_blocking)
        {
            m_attackInputPending = true;
            m_chargeDuration = 0.0f;
        }
        if ((m_attackInputPending || m_charging) && attackHeld)
            m_chargeDuration = (std::min)(
                m_chargeDuration + deltaTime, ChargeLevelThreeTime);

        if (m_attackInputPending && m_chargeDuration >= ChargeActivationTime)
        {
            m_attackInputPending = false;
            m_charging = m_animation->Play(
                "ChargeAttack", false, 0.0f, AnimationBlendDuration);
            if (m_charging)
                m_animation->SetNormalizedTime(ChargeHoldPose);
        }
        else if (m_attackInputPending && attackReleased)
        {
            m_attackInputPending = false;
            m_chargeDuration = 0.0f;
            normalAttackInput = true;
        }

        if (m_charging)
        {
            m_animation->SetNormalizedTime(ChargeHoldPose);
            m_attackCollider->SetEnabled(false);
            m_chargeAttackCollider->SetEnabled(false);
            if (attackReleased)
            {
                m_charging = false;
                if (m_chargeDuration >= ChargeLevelOneTime &&
                    m_stamina >= ChargeStaminaCost)
                {
                    m_chargeDamageRatio = m_chargeDuration >= ChargeLevelThreeTime
                        ? 1.0f : (m_chargeDuration >= ChargeLevelTwoTime ? 0.7f : 0.4f);
                    m_chargeReleasing = m_animation->Play(
                        "ChargeAttack", false, ChargeAttackPlaybackSpeed, 0.0f);
                    m_chargeHitTargets.clear();
                    m_stamina -= ChargeStaminaCost;
                    staminaConsumed = true;
                }
                else
                {
                    m_chargeDuration = 0.0f;
                    m_chargeDamageRatio = 0.0f;
                    m_chargeAttackCollider->SetEnabled(false);
                }
            }
        }

        if (m_chargeReleasing)
        {
            const float progress = m_animation->GetNormalizedTime();
            m_chargeAttackCollider->SetEnabled(
                progress >= ChargeHitStart && progress <= ChargeHitEnd);
            if (m_animation->IsFinished())
            {
                m_chargeReleasing = false;
                m_chargeDuration = 0.0f;
                m_chargeDamageRatio = 0.0f;
                m_chargeAttackCollider->SetEnabled(false);
            }
        }

        if (m_attacking && normalAttackInput && m_attackComboStep < AttackComboCount)
            m_attackComboQueued = true;

        const float attackProgress = m_attacking
            ? m_animation->GetNormalizedTime() : 0.0f;
        if (m_attacking && m_attackComboQueued &&
            m_attackComboStep < AttackComboCount &&
            attackProgress >= AttackComboTransition)
        {
            ++m_attackComboStep;
            m_attackComboQueued = false;
            m_attacking = m_animation->Play(
                AttackAnimations[m_attackComboStep - 1], false,
                AttackPlaybackSpeed, AttackComboBlendDuration);
            m_attackHit = false;
            m_attackCollider->SetEnabled(false);
        }
        else if (m_attacking && m_animation->IsFinished())
        {
            m_attacking = false;
            m_attackComboStep = 0;
            m_attackComboQueued = false;
            m_attackCollider->SetEnabled(false);
        }
        if (m_blocking)
        {
            m_attackCollider->SetEnabled(false);
            m_chargeAttackCollider->SetEnabled(false);
            if (!wasBlocking)
                m_animation->Play("Block", false, BlockPlaybackSpeed,
                    AnimationBlendDuration);
        }
        if (!m_blocking && normalAttackInput && !IsAttacking() &&
            m_attackComboStep == 0)
        {
            m_attacking = m_animation->Play(
                AttackAnimations[0], false, AttackPlaybackSpeed, AnimationBlendDuration);
            if (m_attacking)
            {
                m_attackComboStep = 1;
                m_attackComboQueued = false;
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
        if (!IsAttacking() && !m_blocking)
        {
            const bool blendFromCurrent = m_animation->IsPlaying("Idle") ||
                m_animation->IsPlaying("Walk") || m_animation->IsPlaying("Run") ||
                m_animation->IsPlaying("Attack") || m_animation->IsPlaying("Attack2") ||
                m_animation->IsPlaying("Attack3") ||
                m_animation->IsPlaying("ChargeAttack") ||
                m_animation->IsPlaying("Jump") ||
                m_animation->IsPlaying("Evasion") || m_animation->IsPlaying("Block");
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
    else if (length > 0.0f && !IsAttacking() && !m_blocking)
    {
        rotateTowards(movementX, movementZ);
        const float moveSpeed = dashing ? 6.0f : 3.0f;
        m_position.x += movementX * moveSpeed * deltaTime;
        m_position.z += movementZ * moveSpeed * deltaTime;
        if (dashing)
        {
            m_stamina = std::clamp(
                m_stamina - DashStaminaPerSecond * deltaTime, 0.0f, MaxStamina);
            staminaConsumed = true;
            dashActive = true;
        }
    }

    if (staminaConsumed)
    {
        m_staminaRecoveryDelay = StaminaRecoveryDelay;
    }
    else
    {
        m_staminaRecoveryDelay = std::clamp(
            m_staminaRecoveryDelay - deltaTime, 0.0f, StaminaRecoveryDelay);
        if (m_staminaRecoveryDelay <= 0.0f && !dashActive && !m_evading && !m_blocking)
            m_stamina = std::clamp(
                m_stamina + StaminaRecoveryPerSecond * deltaTime, 0.0f, MaxStamina);
    }

}

bool Player::ResolveAttackHit(
    GameObject& target, DirectX::XMFLOAT3& hitPosition)
{
    if (!m_attacking || !m_attackCollider->IsEnabled() || m_attackHit ||
        !m_attackCollider->HasContactWith(target, ColliderRole::Hurtbox))
    {
        return false;
    }
    const auto attackCenter = m_attackCollider->GetShape().center;
    for (const auto* contact : m_attackCollider->GetContacts())
    {
        if (&contact->GetOwner() != &target ||
            contact->GetRole() != ColliderRole::Hurtbox) continue;
        const auto hurtCenter = contact->GetShape().center;
        hitPosition = {
            (attackCenter.x + hurtCenter.x) * 0.5f,
            (attackCenter.y + hurtCenter.y) * 0.5f,
            (attackCenter.z + hurtCenter.z) * 0.5f
        };
        break;
    }
    target.TakeDamage(AttackDamage);
    m_attackHit = true;
    return true;
}

void Player::ResolveChargeAttackHit(GameObject& target)
{
    if (!m_chargeReleasing || m_chargeDamageRatio <= 0.0f ||
        !m_chargeAttackCollider->IsEnabled() ||
        std::find(m_chargeHitTargets.begin(), m_chargeHitTargets.end(), &target)
            != m_chargeHitTargets.end() ||
        !m_chargeAttackCollider->HasContactWith(target, ColliderRole::Hurtbox))
    {
        return;
    }
    const int damage = static_cast<int>(
        static_cast<float>(target.GetMaxHP()) * m_chargeDamageRatio + 0.5f);
    target.TakeDamage(damage);
    m_chargeHitTargets.push_back(&target);
}

bool Player::CanBlockAttackFrom(const DirectX::XMFLOAT3& attackerPosition) const
{
    if (!m_blocking)
        return false;

    const float toAttackerX = attackerPosition.x - m_position.x;
    const float toAttackerZ = attackerPosition.z - m_position.z;
    const float distance = std::sqrt(
        toAttackerX * toAttackerX + toAttackerZ * toAttackerZ);
    if (distance <= 0.001f)
        return true;

    const float forwardX = std::sin(m_rotation.y);
    const float forwardZ = std::cos(m_rotation.y);
    const float facing = (forwardX * toAttackerX + forwardZ * toAttackerZ) / distance;
    return facing >= BlockFacingThreshold;
}

void Player::ConsumeBlockStamina()
{
    m_stamina = std::clamp(m_stamina - BlockStaminaCost, 0.0f, MaxStamina);
    m_staminaRecoveryDelay = StaminaRecoveryDelay;
}

void Player::ApplyBlockKnockback(const DirectX::XMFLOAT3& attackerPosition)
{
    float awayX = m_position.x - attackerPosition.x;
    float awayZ = m_position.z - attackerPosition.z;
    const float distance = std::sqrt(awayX * awayX + awayZ * awayZ);
    if (distance > 0.001f)
    {
        awayX /= distance;
        awayZ /= distance;
    }
    else
    {
        awayX = -std::sin(m_rotation.y);
        awayZ = -std::cos(m_rotation.y);
    }
    m_position.x += awayX * BlockKnockbackDistance;
    m_position.z += awayZ * BlockKnockbackDistance;
    Terrain::ClampToArena(m_position.x, m_position.z);
    m_position.y = Terrain::HeightAt(m_position.x, m_position.z) + FootOffset;
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

DirectX::XMFLOAT3 Player::GetFootPosition() const
{
    const auto* model = GetComponent<ModelComponent>();
    DirectX::XMFLOAT4X4 leftFootTransform;
    DirectX::XMFLOAT4X4 rightFootTransform;
    if (!model ||
        !m_animation->GetNodeModelTransform(
            "mixamorig:LeftFoot", leftFootTransform) ||
        !m_animation->GetNodeModelTransform(
            "mixamorig:RightFoot", rightFootTransform))
    {
        return { m_position.x, m_position.y - FootOffset, m_position.z };
    }

    const auto modelWorld = model->CreateModelWorldMatrix();
    DirectX::XMFLOAT3 leftFoot;
    DirectX::XMFLOAT3 rightFoot;
    const auto origin = DirectX::XMVectorZero();
    DirectX::XMStoreFloat3(&leftFoot, DirectX::XMVector3TransformCoord(
        origin, DirectX::XMLoadFloat4x4(&leftFootTransform) *
        DirectX::XMLoadFloat4x4(&modelWorld)));
    DirectX::XMStoreFloat3(&rightFoot, DirectX::XMVector3TransformCoord(
        origin, DirectX::XMLoadFloat4x4(&rightFootTransform) *
        DirectX::XMLoadFloat4x4(&modelWorld)));
    return {
        (leftFoot.x + rightFoot.x) * 0.5f,
        (leftFoot.y + rightFoot.y) * 0.5f,
        (leftFoot.z + rightFoot.z) * 0.5f
    };
}
