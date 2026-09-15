#pragma once

#include <vector>

class Boss;
class Camera;
class MiniCopy;
class Player;
class UIManager;

class GameHud
{
public:
    void Update(const Player& player, float deltaTime);
    void Draw(UIManager& ui, const Player& player, const Boss& boss,
        const std::vector<MiniCopy*>& miniCopies, const Camera& camera) const;

private:
    int m_previousPlayerHp = -1;
    float m_playerDamageEffectTimer = 0.0f;
};
