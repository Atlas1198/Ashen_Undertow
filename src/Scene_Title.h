#pragma once

#include "SceneManager.h"

class Scene_Title final : public SceneManager
{
public:
    using SceneManager::SceneManager;

    bool Initialize() override;
};
