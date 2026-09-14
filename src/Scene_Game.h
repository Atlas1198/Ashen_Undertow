#pragma once

#include "Camera.h"
#include "EffectManager.h"
#include "SceneManager.h"
#include "UIManager.h"
#include <vector>

class Player;
class Boss;
class MiniCopy;
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
    UIImageHandle m_effectTexture;
    float m_wonheeHeight = 0;

    Player* m_player = nullptr;
    Boss* m_boss = nullptr;
    std::vector<MiniCopy*> m_miniCopies;
    MiniCopy* m_environmentMiniCopy = nullptr;
    std::vector<Tree*> m_trees;
    const Input& m_input;
    Camera m_camera;
    EffectManager m_effectManager;
    bool m_showColliders = false;
    int m_spawnSequence = 0;
    DirectX::XMFLOAT3 m_previousPlayerPosition {};
    DirectX::XMFLOAT3 m_previousMiniCopyPosition {};
    float m_playerWaterMovement = 0.0f;
    float m_miniCopyWaterMovement = 0.0f;
};
