//-------------------------------------------------
//
//          Boss.h
//
//  ボスの HP と全体攻撃の状態を管理する。
//-------------------------------------------------

#pragma once

#include "GameObject.h"

class AnimationComponent;
class ModelComponent;

// ボス本体。HP に応じた全体攻撃を段階的に進行させる。
class Boss final : public GameObject
{
public:
    // 待機 → 予兆 → 炎の順。描画・効果音もこの状態を参照する。
    enum class GlobalAttackPhase { Cooldown, Warning, Flame };

    bool Initialize(ModelManager& modelManager) override;
    void Update(float deltaTime) override;
    void TakeDamage(int amount);
    void TakeSpecialDamage(int amount);
    float GetVisualScale() const { return m_scale.y; }
    DirectX::XMFLOAT3 GetSpecialAttackTarget() const;
    DirectX::XMFLOAT3 GetMouthPosition() const;
    GlobalAttackPhase GetGlobalAttackPhase() const { return m_globalAttackPhase; }
    float GetGlobalAttackProgress() const;

private:
    AnimationComponent* m_animation = nullptr;
    ModelComponent* m_model = nullptr;
    GlobalAttackPhase m_globalAttackPhase = GlobalAttackPhase::Cooldown;
    // 現在フェーズの残り秒数。Used は一度限りの発動を保証する。
    float m_globalAttackTimer = 0.0f;
    bool m_globalAttackUsed = false;
};
