#pragma once

#include <DirectXMath.h>

namespace GameBalance
{
namespace Player
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

namespace Boss
{
constexpr float VisualScale = 40.0f;
constexpr float MiniCopySpawnInterval = 8.0f;
}

namespace MiniCopy
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
constexpr float DeathEffectDuration = 1.6f;
}

namespace Scene
{
constexpr int MaxMiniCopies = 4;
constexpr float MinChargeHitStop = 0.105f;
constexpr float MaxChargeHitStop = 0.15f;
}
}
