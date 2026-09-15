#pragma once

#include "Camera.h"
#include "EffectManager.h"
#include "SceneManager.h"
#include "SpecialAttack.h"
#include "UIManager.h"
#include <array>

class Input;
class Player;
class Boss;
class GameObject;

class Scene_Tutorial final : public SceneManager
{
public:
    Scene_Tutorial(ModelManager& modelManager, const Input& input)
        : SceneManager(modelManager), m_input(input) {}

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
    Player* m_player = nullptr;
    Boss* m_boss = nullptr;
    GameObject* m_dummy = nullptr;
    Camera m_camera;
    EffectManager m_effectManager;
    SpecialAttack m_specialAttack;
    UIImageHandle m_effectTexture;
    UIImageHandle m_stepsTexture;
    UIImageHandle m_gameTexture;
    UIImageHandle m_pressEnterTexture;
    std::array<bool, TutorialStepCount> m_completed {};
    DirectX::XMFLOAT3 m_previousPlayerPosition {};
    float m_moveDistance = 0.0f;
    float m_dashDistance = 0.0f;
    float m_guardDuration = 0.0f;
    float m_specialEffectTimer = 0.0f;
    float m_time = 0.0f;
    float m_fade = 0.0f;
    int m_step = 0;
    int m_comboHits = 0;
    bool m_wasAirborne = false;
    bool m_wasEvading = false;
    bool m_wasAttacking = false;
    bool m_leaving = false;
};
