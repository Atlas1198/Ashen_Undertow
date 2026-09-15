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
    void Draw(UIManager& ui, const Player& player, const Boss& boss,
        const std::vector<MiniCopy*>& miniCopies, const Camera& camera) const;
};
