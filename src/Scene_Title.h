#pragma once

#include "SceneManager.h"

class Title final : public SceneManager
{
public:
    bool Initialize() override;
    void Update(float deltaTime) override;
    void Draw(Renderer& renderer) override;
};
