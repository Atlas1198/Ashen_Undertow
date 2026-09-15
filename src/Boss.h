#pragma once

#include "GameObject.h"

class AnimationComponent;
class ModelComponent;

class Boss final : public GameObject
{
public:
    bool Initialize(ModelManager& modelManager) override;
    void Update(float deltaTime) override;
    bool ConsumeSpawnRequest();
    float GetVisualScale() const { return m_scale.y; }
    DirectX::XMFLOAT3 GetSpecialAttackTarget() const;

private:
    AnimationComponent* m_animation = nullptr;
    ModelComponent* m_model = nullptr;
    float m_spawnTimer = 2.0f;
};
