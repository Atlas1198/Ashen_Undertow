//-------------------------------------------------
//
//          Scene_Game.h
//
//  本編シーンの進行、敵の出現、戦闘、演出を統括する。
//-------------------------------------------------

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
class AudioManager;

// 本編シーン。敵の出現、戦闘結果、演出、UI と画面遷移を統括する。
class Scene_Game final : public SceneManager
{
public:
    Scene_Game(ModelManager& modelManager, const Input& input, AudioManager& audio);

    bool Initialize() override;
    bool InitializeUI(UIManager& ui) override;

private:
    void UpdateCameraGroundConstraint();
    void UpdatePlayerEffects(float deltaTime);
    void UpdateMiniCopySpawning(float deltaTime);
    void QueueMiniCopySpawn(const DirectX::XMFLOAT3& position);
    void SpawnMiniCopyAt(const DirectX::XMFLOAT3& position);
    DirectX::XMFLOAT3 MakeMiniCopySpawnPosition(float angle, float radius) const;
    float NextSpawnRandom();
    void ResolveCombat();
    void UpdateCameraAndTrees();
    void UpdateEnvironmentEffects(float deltaTime);
    void UpdateBossGlobalAttack();
    void TryLaunchSpecialAttack();
    void UpdateSpecialAttack(float deltaTime);
    void OnUpdate(float deltaTime) override;
    void OnAfterUpdate(float deltaTime) override;
    // ヒットストップや開始／終了演出中はゲーム世界の更新を止める。
    bool IsSimulationPaused() const override
    {
        return m_hitStopThisFrame || m_deathScreenTime >= 0.0f ||
            m_clearScreenTime >= 0.0f || m_gameStartTime < 2.2f;
    }
    void OnDraw(Renderer& renderer) override;
    void OnDrawUI(UIManager& ui) override;
    UIImageHandle m_effectTexture;
    UIImageHandle m_youDiedTexture;
    UIImageHandle m_titleTexture;
    UIImageHandle m_retryTexture;
    UIImageHandle m_clearTexture;
    UIImageHandle m_gameStartTexture;
    UIImageHandle m_specialPromptTexture;
    UIImageHandle m_staminaShortageTexture;
    GameHud m_hud;

    // 実体の所有者は SceneManager。ここでは参照のみ保持する。
    Player* m_player = nullptr;
    Boss* m_boss = nullptr;
    std::vector<MiniCopy*> m_miniCopies;
    MiniCopy* m_environmentMiniCopy = nullptr;
    std::vector<Tree*> m_trees;
    const Input& m_input;
    AudioManager& m_audio;
    Camera m_camera;
    EffectManager m_effectManager;
    SpecialAttack m_specialAttack;
    float m_specialAttackEffectTimer = 0.0f;
    bool m_showColliders = false;
    int m_spawnSequence = 0;
    // 予兆の雷から実体出現までを遅延させるキュー。
    struct PendingMiniCopySpawn
    {
        DirectX::XMFLOAT3 position {};
        float delay = 0.0f;
        bool lightningStarted = false;
    };
    std::vector<PendingMiniCopySpawn> m_pendingMiniCopySpawns;
    // 湧き間隔は秒単位。乱数状態を保持して出現位置を再現可能にする。
    float m_miniCopySpawnTimer = 6.0f;
    int m_spawnWaveSequence = 0;
    std::uint32_t m_spawnRandomState = 0x51f15e5du;
    // 前フレーム位置と各タイマーから足音・水しぶきの頻度／強さを決める。
    DirectX::XMFLOAT3 m_previousPlayerPosition {};
    DirectX::XMFLOAT3 m_previousMiniCopyPosition {};
    float m_playerWaterMovement = 0.0f;
    float m_miniCopyWaterMovement = 0.0f;
    float m_chargeAuraSpawnTimer = 0.0f;
    float m_footstepDustTimer = 0.0f;
    float m_playerFootstepTimer = 0.0f;
    int m_playerFootstepSequence = 0;
    float m_playerWaterSplashTimer = 0.0f;
    std::vector<DirectX::XMFLOAT3> m_previousMiniCopyEffectPositions;
    std::vector<float> m_miniCopyWaterSplashTimers;
    std::vector<float> m_miniCopyFootstepTimers;
    int m_enemyFootstepSequence = 0;
    std::vector<bool> m_miniCopyWasDead;
    // 攻撃命中時の一時停止と、剣軌跡の直近ポーズ。
    float m_hitStopTimer = 0.0f;
    bool m_hitStopThisFrame = false;
    std::array<SwordTrailPose, 4> m_swordTrailHistory {};
    size_t m_swordTrailHistoryCount = 0;
    bool m_wasPlayerEvading = false;
    bool m_bossFlameSoundActive = false;
    float m_bossAttackVisualTime = 0.0f;
    float m_staminaShortageNoticeTime = 0.0f;
    // 画面遷移用タイマーは秒単位。負値は画面がまだ始まっていない状態。
    float m_gameStartTime = 0.0f;
    float m_deathScreenTime = -1.0f;
    int m_deathMenuChoice = 1;
    bool m_deathChoiceAxisReady = true;
    float m_clearSettleTime = 0.0f;
    float m_clearScreenTime = -1.0f;
};
