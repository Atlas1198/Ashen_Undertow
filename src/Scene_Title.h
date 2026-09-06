#pragma once

#include "SceneManager.h"

class Title final : public SceneManager
{
public:
    using SceneManager::SceneManager;

    bool Initialize() override;
};
