#pragma once

#include "UIManager.h"
#include <vector>

class Boss;
class Camera;
class MiniCopy;
class Player;
class UIManager;

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
    int m_previousPlayerHp = -1;
    float m_playerDamageEffectTimer = 0.0f;
    float m_elapsedTime = 0.0f;
    UIImageHandle m_vitalityIcon;
    UIImageHandle m_staminaIcon;
    UIImageHandle m_ultimateIcon;
    UIImageHandle m_bossIcon;
};
