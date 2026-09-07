#pragma once

#include "Camera.h"
#include "SceneManager.h"

class Player;
class Enemy;
class Input;

class Scene_Game final : public SceneManager
{
public:
    Scene_Game(ModelManager& modelManager, const Input& input);

    bool Initialize() override;

private:
    void OnUpdate(float deltaTime) override;
    void OnAfterUpdate(float deltaTime) override;
    void OnDraw(Renderer& renderer) override;

    Player* m_player = nullptr;
    Enemy* m_enemy = nullptr;
    const Input& m_input;
    Camera m_camera;
};
