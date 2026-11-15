//-------------------------------------------------
//
//          GameBalance.h
//
//  ゲーム内の距離・時間・ダメージなどの調整値を集約する。
//-------------------------------------------------

#pragma once

#include <DirectXMath.h>

// ゲームプレイ調整値の集約。距離はワールド単位、時間は秒、
// AttackHitStart/End などのアニメーション区間は 0～1 の正規化時刻。
namespace GameBalance
{
// プレイヤーの移動・攻撃・防御・スタミナ。
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
constexpr float ThirdAttackColliderLengthScale = 1.08f;
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

// ボスの見た目倍率と全体攻撃の発動条件／継続時間。
namespace Boss
{
constexpr float VisualScale = 40.0f;
constexpr bool GlobalAttackTestAtStart = false;
constexpr float GlobalAttackTriggerHpRatio = 0.1f;
constexpr float GlobalAttackWarningDuration = 13.0f;
constexpr float GlobalAttackFlameDuration = 4.0f;
constexpr int GlobalAttackDamage = 100;
}

// 雑魚敵の索敵距離、移動、攻撃、撃破時の効果。
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

// 出現数、画面演出、ヒットストップなどシーン側の調整値。
namespace Scene
{
constexpr int InitialMiniCopies = 8;
constexpr int MaxMiniCopies = 12;
constexpr float MiniCopySpawnWarning = 0.28f;
constexpr float MiniCopySpawnMinRadius = 10.0f;
constexpr float MiniCopySpawnMaxRadius = 15.0f;
constexpr float MinChargeHitStop = 0.105f;
constexpr float MaxChargeHitStop = 0.15f;
constexpr float ClearSettleDuration = 0.6f;
constexpr float SpecialPromptPulseSpeed = 4.5f;
constexpr float StaminaShortageNoticeDuration = 2.0f;
}

// 特殊攻撃のゲージ増加量、追尾、命中、寿命。
namespace SpecialAttack
{
constexpr float GaugePerDefeat = 10.0f;
constexpr int Damage = 1000;
constexpr float Speed = 20.0f;
constexpr float HomingResponsiveness = 8.0f;
constexpr float HitRadius = 3.5f;
constexpr float MaximumLifetime = 5.0f;
}
}
