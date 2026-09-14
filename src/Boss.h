#pragma once

#include "GameObject.h"

class AnimationComponent;

class Boss final : public GameObject
{
public:
    bool Initialize(ModelManager& modelManager) override;
    void Update(float deltaTime) override;
    bool ConsumeSpawnRequest();
    float GetVisualScale() const { return m_scale.y; }

private:
    AnimationComponent* m_animation = nullptr;
    float m_spawnTimer = 2.0f;
};
