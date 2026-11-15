//-------------------------------------------------
//
//          GameHud.h
//
//  プレイヤーと敵の状態を HUD 表示へ変換する。
//-------------------------------------------------

#pragma once

#include "UIManager.h"
#include <vector>

class Boss;
class Camera;
class MiniCopy;
class Player;
class UIManager;

// HP・スタミナ・特殊ゲージと敵 HP を UI 描画命令へ変換する。
class GameHud
{
public:
    bool InitializeUI(UIManager& ui);
    void Update(const Player& player, float deltaTime);
    void DrawPlayerStatus(UIManager& ui, const Player& player,
        bool emphasizeStamina = false) const;
    void Draw(UIManager& ui, const Player& player, const Boss& boss,
        const std::vector<MiniCopy*>& miniCopies, const Camera& camera) const;

private:
    // 前フレーム HP との差で被弾演出を起動する。タイマーは秒単位。
    int m_previousPlayerHp = -1;
    float m_playerDamageEffectTimer = 0.0f;
    float m_elapsedTime = 0.0f;
    UIImageHandle m_vitalityIcon;
    UIImageHandle m_staminaIcon;
    UIImageHandle m_ultimateIcon;
    UIImageHandle m_bossIcon;
};
