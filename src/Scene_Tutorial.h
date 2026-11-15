//-------------------------------------------------
//
//          Scene_Tutorial.h
//
//  チュートリアルの課題判定と進行を管理する。
//-------------------------------------------------

#pragma once

#include "Camera.h"
#include "EffectManager.h"
#include "GameHud.h"
#include "SceneManager.h"
#include "SpecialAttack.h"
#include "UIManager.h"
#include <array>

class Input;
class AudioManager;
class Player;
class Boss;
class GameObject;

// 操作を一段階ずつ練習し、達成状況に応じて次の課題へ進めるシーン。
class Scene_Tutorial final : public SceneManager
{
public:
    Scene_Tutorial(ModelManager& modelManager, const Input& input,
        AudioManager& audio)
        : SceneManager(modelManager), m_input(input), m_audio(audio) {}

    bool Initialize() override;
    bool InitializeUI(UIManager& ui) override;

private:
    static constexpr int TutorialStepCount = 8;
    void OnUpdate(float deltaTime) override;
    void OnAfterUpdate(float deltaTime) override;
    void OnDraw(Renderer& renderer) override;
    void OnDrawUI(UIManager& ui) override;
    void AdvanceStep();
    void PlaceDummyInFrontOfPlayer();
    void PlaceBossInFrontOfPlayer();
    DirectX::XMFLOAT3 GetDummyTarget() const;

    const Input& m_input;
    AudioManager& m_audio;
    Player* m_player = nullptr;
    Boss* m_boss = nullptr;
    GameObject* m_dummy = nullptr;
    Camera m_camera;
    EffectManager m_effectManager;
    GameHud m_hud;
    SpecialAttack m_specialAttack;
    UIImageHandle m_effectTexture;
    UIImageHandle m_stepsTexture;
    UIImageHandle m_clearTexture;
    UIImageHandle m_pressEnterTexture;
    // 各課題の達成状態。移動距離・ガード時間などは現在の判定用に積算する。
    std::array<bool, TutorialStepCount> m_completed {};
    DirectX::XMFLOAT3 m_previousPlayerPosition {};
    float m_moveDistance = 0.0f;
    float m_dashDistance = 0.0f;
    float m_guardDuration = 0.0f;
    float m_specialEffectTimer = 0.0f;
    float m_footstepTimer = 0.0f;
    float m_time = 0.0f;
    float m_completionTime = 0.0f;
    float m_fade = 0.0f;
    // 現在の課題番号と、連続攻撃／足音の進行状態。
    int m_step = 0;
    int m_comboHits = 0;
    int m_footstepSequence = 0;
    bool m_wasAirborne = false;
    bool m_wasEvading = false;
    bool m_wasAttacking = false;
    bool m_leaving = false;
};
