#pragma once

#include "Camera.h"
#include "SceneManager.h"
#include "UIManager.h"

class Player;
class Enemy;
class Tree;
class Input;

class Scene_Game final : public SceneManager
{
public:
    Scene_Game(ModelManager& modelManager, const Input& input);

    bool Initialize() override;
    bool InitializeUI(UIManager& ui) override;

private:
    void OnUpdate(float deltaTime) override;
    void OnAfterUpdate(float deltaTime) override;
    void OnDraw(Renderer& renderer) override;
    void OnDrawUI(UIManager& ui) override;
    UIImageHandle m_wonheeImage;
    float m_wonheeHeight = 0;

    Player* m_player = nullptr;
    Enemy* m_enemy = nullptr;
    Tree* m_tree = nullptr;
    const Input& m_input;
    Camera m_camera;
    bool m_showColliders = false;
    DirectX::XMFLOAT3 m_previousPlayerPosition {};
    DirectX::XMFLOAT3 m_previousEnemyPosition {};
    float m_playerWaterMovement = 0.0f;
    float m_enemyWaterMovement = 0.0f;
};
