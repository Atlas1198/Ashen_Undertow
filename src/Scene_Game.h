#pragma once

#include <memory>

#include "Player.h"
#include "SceneManager.h"

class GameScene final : public SceneManager
{
public:
    explicit GameScene(ModelManager& modelManager);

    bool Initialize() override;
    void Update(float deltaTime) override;
    void Draw(Renderer& renderer) override;

private:
    ModelManager& m_modelManager;
    std::unique_ptr<Player> m_player;
};
