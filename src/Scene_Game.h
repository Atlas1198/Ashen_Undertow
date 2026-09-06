#pragma once

#include <memory>

#include "Player.h"
#include "SceneManager.h"

class GameScene final : public SceneManager
{
public:
    void Initialize() override;
    void Update() override;

private:
    std::unique_ptr<Player> m_player;
};
