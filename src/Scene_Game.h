#pragma once

#include "Player.h"
#include "SceneManager.h"

class GameScene final : public SceneManager
{
public:
    explicit GameScene(ModelManager& modelManager);

    bool Initialize() override;

private:
    void OnDraw(Renderer& renderer) override;

    Player* m_player = nullptr;
};
