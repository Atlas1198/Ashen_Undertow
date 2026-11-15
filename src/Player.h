//-------------------------------------------------
//
//          Player.h
//
//  プレイヤーの移動・戦闘・スタミナ・特殊ゲージを管理する。
//-------------------------------------------------

#pragma once

#include "GameObject.h"
#include "ModelHandle.h"
#include <vector>

class Input;
class AnimationComponent;
class BoneSegmentColliderAttachment;
class Collider;
class Rigidbody;
class SphereCollider;

// 入力、移動、スタミナ、近接戦闘をまとめるプレイヤー本体。
// 当たり判定とアニメーションはコンポーネントに委譲する。
class Player final : public GameObject
{
public:
    explicit Player(const Input& input) : m_input(input) {}
    ~Player() override = default;

    bool Initialize(ModelManager& modelManager) override;
    void Update(float deltaTime) override;
    // 物理更新後に地形との接触を補正する。
    void ResolveTerrainContact();
    // 接触中の攻撃コライダーを調べ、命中位置を返す。
    bool ResolveAttackHit(GameObject& target, DirectX::XMFLOAT3& hitPosition);
    // 溜め攻撃の命中と威力を返す。威力はヒット演出にも使う。
    bool ResolveChargeAttackHit(GameObject& target,
        DirectX::XMFLOAT3& hitPosition, float& chargePower);
    void SetMovementYaw(float yaw) { m_movementYaw = yaw; }
    bool IsAttacking() const
    {
        return m_attacking || m_attackInputPending || m_charging || m_chargeReleasing;
    }
    bool IsBlocking() const { return m_blocking; }
    bool IsCharging() const { return m_charging; }
    bool IsEvading() const { return m_evading; }
    bool IsGrounded() const { return !m_jumping; }
    float GetChargeDuration() const { return m_chargeDuration; }
    bool CanBlockAttackFrom(const DirectX::XMFLOAT3& attackerPosition) const;
    void ConsumeBlockStamina();
    void ApplyBlockKnockback(const DirectX::XMFLOAT3& attackerPosition);
    float GetStamina() const { return m_stamina; }
    float GetMaxStamina() const { return MaxStamina; }
    // HUD に一度だけ通知し、同じ不足表示の繰り返しを防ぐ。
    bool ConsumeChargeStaminaShortageNotice()
    {
        const bool noticed = m_chargeStaminaShortageNotice;
        m_chargeStaminaShortageNotice = false;
        return noticed;
    }
    float GetSpecialGauge() const { return m_specialGauge; }
    float GetMaxSpecialGauge() const { return MaxSpecialGauge; }
    bool IsSpecialGaugeFull() const { return m_specialGauge >= MaxSpecialGauge; }
    void AddSpecialGauge(float amount);
    bool ConsumeSpecialGauge();
    // 特殊攻撃の発射が成立したときだけ、進行中の近接攻撃を中断する。
    void InterruptCombatForSpecialAttack();
    DirectX::XMFLOAT3 GetFootPosition() const;
    ModelHandle GetModelHandle() const;
    DirectX::XMFLOAT4 GetRimLightColor() const override;
    bool GetSwordTrailSegment(DirectX::XMFLOAT3& start,
        DirectX::XMFLOAT3& end) const;
    bool IsDeathAnimationFinished() const;

private:    
    bool UpdateGrounding();
    void FinishEvasionIfNeeded();
    void UpdateTraversalAction(bool grounded, bool dashRequested,
        float movementX, float movementZ, bool& staminaConsumed);
    void UpdateJumpAnimation();
    void UpdateCombat(float deltaTime, float movementLength,
        bool dashing, bool wasBlocking, bool& staminaConsumed);
    bool UpdateChargeAttack(float deltaTime, bool attackPressed,
        bool attackHeld, bool attackReleased, bool& staminaConsumed);
    void UpdateNormalAttack(bool normalAttackInput, bool wasBlocking);
    void UpdateLocomotionAnimation(float movementLength, bool dashing);
    void UpdateMovement(float deltaTime, float movementLength,
        float movementX, float movementZ, bool dashing,
        bool& staminaConsumed, bool& dashActive);
    // 消費がない時間だけ回復待ちを進め、待ち時間後に回復する。
    void UpdateStamina(float deltaTime, bool staminaConsumed, bool dashActive);
    void SetStandingHurtboxesEnabled(bool enabled);

    static constexpr float MaxStamina = 100.0f;
    static constexpr float MaxSpecialGauge = 100.0f;
    // 参照とコンポーネントポインタの寿命はシーン／GameObject が管理する。
    const Input& m_input;
    AnimationComponent* m_animation = nullptr;
    BoneSegmentColliderAttachment* m_attackColliderAttachment = nullptr;
    SphereCollider* m_evasionCollider = nullptr;
    Collider* m_attackCollider = nullptr;
    Collider* m_chargeAttackCollider = nullptr;
    std::vector<Collider*> m_standingHurtboxes;
    // 一回の溜め攻撃で同じ対象に複数回ダメージを与えないための記録。
    std::vector<GameObject*> m_chargeHitTargets;
    Rigidbody* m_rigidbody = nullptr;
    float m_movementYaw = 0.0f;
    float m_evasionDirectionX = 0.0f;
    float m_evasionDirectionZ = 0.0f;
    // スタミナとゲージは 0～100。時間を表す float は秒単位。
    float m_stamina = MaxStamina;
    float m_staminaRecoveryDelay = 0.0f;
    float m_specialGauge = 0.0f;
    float m_specialRimTime = 0.0f;
    float m_chargeDuration = 0.0f;
    float m_chargeDamageRatio = 0.0f;
    // コンボ段数と入力待ちを分け、アニメーション中の先行入力を保持する。
    int m_attackComboStep = 0;
    bool m_attacking = false;
    bool m_attackComboQueued = false;
    bool m_attackInputPending = false;
    bool m_charging = false;
    bool m_chargeReleasing = false;
    bool m_chargeStaminaShortageNotice = false;
    bool m_blocking = false;
    bool m_attackHit = false;
    bool m_evading = false;
    bool m_jumping = false;
    bool m_deathStarted = false;
};
