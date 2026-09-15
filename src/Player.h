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

class Player final : public GameObject
{
public:
    explicit Player(const Input& input) : m_input(input) {}
    ~Player() override = default;

    bool Initialize(ModelManager& modelManager) override;
    void Update(float deltaTime) override;
    void ResolveTerrainContact();
    bool ResolveAttackHit(GameObject& target, DirectX::XMFLOAT3& hitPosition);
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
    void UpdateStamina(float deltaTime, bool staminaConsumed, bool dashActive);
    void SetStandingHurtboxesEnabled(bool enabled);

    static constexpr float MaxStamina = 100.0f;
    static constexpr float MaxSpecialGauge = 100.0f;
    const Input& m_input;
    AnimationComponent* m_animation = nullptr;
    BoneSegmentColliderAttachment* m_attackColliderAttachment = nullptr;
    SphereCollider* m_evasionCollider = nullptr;
    Collider* m_attackCollider = nullptr;
    Collider* m_chargeAttackCollider = nullptr;
    std::vector<Collider*> m_standingHurtboxes;
    std::vector<GameObject*> m_chargeHitTargets;
    Rigidbody* m_rigidbody = nullptr;
    float m_movementYaw = 0.0f;
    float m_evasionDirectionX = 0.0f;
    float m_evasionDirectionZ = 0.0f;
    float m_stamina = MaxStamina;
    float m_staminaRecoveryDelay = 0.0f;
    float m_specialGauge = 0.0f;
    float m_specialRimTime = 0.0f;
    float m_chargeDuration = 0.0f;
    float m_chargeDamageRatio = 0.0f;
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
