#pragma once

#include "GameObject.h"
#include "ModelHandle.h"
#include <vector>

class Input;
class AnimationComponent;
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
    void ResolveAttackHit(GameObject& target);
    void SetMovementYaw(float yaw) { m_movementYaw = yaw; }
    bool IsAttacking() const { return m_attacking; }
    ModelHandle GetModelHandle() const;

private:    
    const Input& m_input;
    AnimationComponent* m_animation = nullptr;
    SphereCollider* m_evasionCollider = nullptr;
    Collider* m_attackCollider = nullptr;
    std::vector<Collider*> m_standingHurtboxes;
    Rigidbody* m_rigidbody = nullptr;
    float m_movementYaw = 0.0f;
    float m_evasionDirectionX = 0.0f;
    float m_evasionDirectionZ = 0.0f;
    bool m_attacking = false;
    bool m_attackHit = false;
    bool m_evading = false;
    bool m_jumping = false;
};
