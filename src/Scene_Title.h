#pragma once

#include "SceneManager.h"

class Title final : public SceneManager
{
public:
    void Initialize() override;
    void Update() override;
};
