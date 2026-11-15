//-------------------------------------------------
//
//          MiniCopy.h
//
//  雑魚敵の追跡・攻撃・撃破状態を管理する。
//-------------------------------------------------

#pragma once

#include "GameObject.h"
#include <vector>

class AnimationComponent;
class Boss;
class Collider;
class Player;

// プレイヤーを追跡する雑魚敵。撃破時にはボス HP と特殊ゲージにも影響する。
class MiniCopy final : public GameObject
{
public:
    MiniCopy(Player& target, Boss& boss) : m_target(target), m_boss(boss) {}
    bool Initialize(ModelManager& modelManager) override;
    void Update(float deltaTime) override;
    bool ResolveAttackHit(GameObject& target,
        DirectX::XMFLOAT3& blockPosition);
    // 死亡演出へ移行する。報酬とボスへの報告は一度だけ行う。
    void EnterDefeatedState();
    void Respawn(const DirectX::XMFLOAT3& position);
    DirectX::XMFLOAT3 GetHPBarPosition() const;
    float GetDissolveAmount() const override;
    float GetDissolveBaseY() const override { return m_position.y; }

private:
    Player& m_target;
    Boss& m_boss;
    AnimationComponent* m_animation = nullptr;
    Collider* m_attackCollider = nullptr;
    Collider* m_leftAttackCollider = nullptr;
    Collider* m_headCollider = nullptr;
    std::vector<Collider*> m_hurtboxes;
    // 行動タイマーは秒単位。戦闘・攻撃・走行の状態は別々に保持する。
    float m_attackCooldown = 0.0f;
    bool m_inCombat = false;
    bool m_attacking = false;
    bool m_attackHit = false;
    bool m_running = false;
    // 重複報酬を防ぐフラグと、消滅演出の経過秒数。
    bool m_deathReported = false;
    float m_deathEffectTime = 0.0f;
};
