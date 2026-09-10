#pragma once

#include "GameObject.h"
#include "ModelHandle.h"

class Input;
class AnimationComponent;
class Rigidbody;

class Player final : public GameObject
{
public:
    explicit Player(const Input& input) : m_input(input) {}
    ~Player() override = default;

    bool Initialize(ModelManager& modelManager) override;
    void Update(float deltaTime) override;
    void ResolveTerrainContact();
    void SetMovementYaw(float yaw) { m_movementYaw = yaw; }
    ModelHandle GetModelHandle() const;

private:    
    const Input& m_input;
    AnimationComponent* m_animation = nullptr;
    Rigidbody* m_rigidbody = nullptr;
    float m_movementYaw = 0.0f;
    bool m_attacking = false;
    bool m_jumping = false;
};
