#pragma once

#include "GameObject.h"
#include <vector>

class AnimationComponent;
class Boss;
class Collider;
class Player;

class MiniCopy final : public GameObject
{
public:
    MiniCopy(Player& target, Boss& boss) : m_target(target), m_boss(boss) {}
    bool Initialize(ModelManager& modelManager) override;
    void Update(float deltaTime) override;
    bool ResolveAttackHit(GameObject& target,
        DirectX::XMFLOAT3& blockPosition);
    void EnterDefeatedState();
    void Respawn(const DirectX::XMFLOAT3& position);
    DirectX::XMFLOAT3 GetHPBarPosition() const;
    float GetDissolveAmount() const override;
    float GetDissolveBaseY() const override { return m_position.y; }

private:
    Player& m_target;
    Boss& m_boss;
    AnimationComponent* m_animation = nullptr;
    Collider* m_attackCollider = nullptr;
    Collider* m_leftAttackCollider = nullptr;
    Collider* m_headCollider = nullptr;
    std::vector<Collider*> m_hurtboxes;
    float m_attackCooldown = 0.0f;
    bool m_inCombat = false;
    bool m_attacking = false;
    bool m_attackHit = false;
    bool m_running = false;
    bool m_deathReported = false;
    float m_deathEffectTime = 0.0f;
};
