#pragma once

#include "GameObject.h"

class AnimationComponent;
class ModelComponent;

class Boss final : public GameObject
{
public:
    enum class GlobalAttackPhase { Cooldown, Warning, Flame };

    bool Initialize(ModelManager& modelManager) override;
    void Update(float deltaTime) override;
    float GetVisualScale() const { return m_scale.y; }
    DirectX::XMFLOAT3 GetSpecialAttackTarget() const;
    DirectX::XMFLOAT3 GetMouthPosition() const;
    GlobalAttackPhase GetGlobalAttackPhase() const { return m_globalAttackPhase; }
    float GetGlobalAttackProgress() const;

private:
    AnimationComponent* m_animation = nullptr;
    ModelComponent* m_model = nullptr;
    GlobalAttackPhase m_globalAttackPhase = GlobalAttackPhase::Cooldown;
    float m_globalAttackTimer = 0.0f;
    bool m_globalAttackUsed = false;
};
