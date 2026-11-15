//-------------------------------------------------
//
//          SpecialAttackTests.cpp
//
//  追尾弾の発射、命中、再発射、寿命を検証する。
//-------------------------------------------------

#include "SpecialAttack.h"

#include <cassert>

int main()
{
    const DirectX::XMFLOAT3 start { 0.0f, 1.0f, 0.0f };
    const DirectX::XMFLOAT3 target { 4.0f, 5.0f, 20.0f };
    SpecialAttack attack;
    assert(attack.Launch(start, target));
    assert(attack.IsActive());
    assert(!attack.Launch(start, target));

    bool hit = false;
    for (int frame = 0; frame < 600 && attack.IsActive(); ++frame)
        hit = attack.Update(1.0f / 60.0f, target) || hit;
    assert(hit);
    assert(!attack.IsActive());
    assert(attack.Launch(start, target));
}
