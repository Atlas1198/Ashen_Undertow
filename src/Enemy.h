#pragma once

#include "GameObject.h"
#include "ModelManager.h"

class Enemy final : public GameObject
{
public:
    bool Initialize(ModelManager& modelManager) override;
    void Update(float deltaTime) override;
    void Draw(Renderer& renderer) override;

private:
    ModelHandle m_modelHandle;
};
