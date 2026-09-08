#pragma once

#include "GameObject.h"
#include "ModelHandle.h"

class Input;
class AnimationComponent;

class Player final : public GameObject
{
public:
    explicit Player(const Input& input) : m_input(input) {}
    ~Player() override = default;

    bool Initialize(ModelManager& modelManager) override;
    void Update(float deltaTime) override;
    ModelHandle GetModelHandle() const;

private:    
    const Input& m_input;
    AnimationComponent* m_animation = nullptr;
    bool m_attacking = false;
};
