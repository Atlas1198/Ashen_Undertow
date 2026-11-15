//-------------------------------------------------
//
//          GameObjectHPTests.cpp
//
//  HP の上下限、死亡判定、個体ごとの独立性を検証する。
//-------------------------------------------------

#include "GameObject.h"
#include <cassert>
#include <climits>

struct HPObject : GameObject
{
    using GameObject::InitializeHP;
    bool Initialize(ModelManager&) override { return true; }
};

int main()
{
    HPObject player, enemy;
    player.InitializeHP(100);
    enemy.InitializeHP(1000);
    assert(player.GetHP() == 100 && player.GetMaxHP() == 100);
    player.TakeDamage(30);
    assert(player.GetHP() == 70 && !player.IsDead());
    assert(enemy.GetHP() == 1000); // Objectごとに独立。
    player.TakeDamage(-10);
    player.Heal(-10);
    player.TakeDamage(0);
    player.Heal(0);
    assert(player.GetHP() == 70);
    player.Heal(20);
    assert(player.GetHP() == 90);
    player.Heal(INT_MAX);
    assert(player.GetHP() == 100);
    player.TakeDamage(100);
    assert(player.GetHP() == 0 && player.IsDead());
    player.TakeDamage(INT_MAX);
    assert(player.GetHP() == 0);
    player.Heal(10);
    assert(player.GetHP() == 10 && !player.IsDead());
    player.InitializeHP(INT_MAX);
    player.Heal(INT_MAX);
    assert(player.GetHP() == INT_MAX);
    player.TakeDamage(INT_MAX);
    assert(player.IsDead());
    player.InitializeHP(-1);
    player.Heal(INT_MAX);
    assert(player.GetHP() == 0 && player.GetMaxHP() == 0);
}
