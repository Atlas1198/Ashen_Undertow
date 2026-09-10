#pragma once

#include "GameObject.h"

class AnimationComponent;
class Collider;
class Player;

class Enemy final : public GameObject
{
public:
    explicit Enemy(Player& target) : m_target(target) {}
    bool Initialize(ModelManager& modelManager) override;
    void Update(float deltaTime) override;
    void ResolveAttackHit(GameObject& target);

private:
    Player& m_target;
    AnimationComponent* m_animation = nullptr;
    Collider* m_attackCollider = nullptr;
    float m_attackCooldown = 0.0f;
    bool m_inCombat = false;
    bool m_attacking = false;
    bool m_attackHit = false;
};
