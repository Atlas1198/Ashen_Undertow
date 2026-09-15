#pragma once

#include "Camera.h"
#include "EffectManager.h"
#include "GameHud.h"
#include "SpecialAttack.h"
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
    void UpdateCameraGroundConstraint();
    void UpdatePlayerEffects(float deltaTime);
    void UpdateMiniCopySpawning();
    void ResolveCombat();
    void UpdateCameraAndTrees();
    void UpdateEnvironmentEffects(float deltaTime);
    void TryLaunchSpecialAttack();
    void UpdateSpecialAttack(float deltaTime);
    void OnUpdate(float deltaTime) override;
    void OnAfterUpdate(float deltaTime) override;
    bool IsSimulationPaused() const override { return m_hitStopThisFrame; }
    void OnDraw(Renderer& renderer) override;
    void OnDrawUI(UIManager& ui) override;
    UIImageHandle m_effectTexture;
    GameHud m_hud;

    Player* m_player = nullptr;
    Boss* m_boss = nullptr;
    std::vector<MiniCopy*> m_miniCopies;
    MiniCopy* m_environmentMiniCopy = nullptr;
    std::vector<Tree*> m_trees;
    const Input& m_input;
    Camera m_camera;
    EffectManager m_effectManager;
    SpecialAttack m_specialAttack;
    float m_specialAttackEffectTimer = 0.0f;
    bool m_showColliders = false;
    int m_spawnSequence = 0;
    DirectX::XMFLOAT3 m_previousPlayerPosition {};
    DirectX::XMFLOAT3 m_previousMiniCopyPosition {};
    float m_playerWaterMovement = 0.0f;
    float m_miniCopyWaterMovement = 0.0f;
    float m_chargeAuraSpawnTimer = 0.0f;
    float m_footstepDustTimer = 0.0f;
    float m_playerWaterSplashTimer = 0.0f;
    std::vector<DirectX::XMFLOAT3> m_previousMiniCopyEffectPositions;
    std::vector<float> m_miniCopyWaterSplashTimers;
    std::vector<bool> m_miniCopyWasDead;
    float m_hitStopTimer = 0.0f;
    bool m_hitStopThisFrame = false;
    std::array<SwordTrailPose, 4> m_swordTrailHistory {};
    size_t m_swordTrailHistoryCount = 0;
    bool m_wasPlayerEvading = false;
};
