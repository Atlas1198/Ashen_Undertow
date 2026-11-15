//-------------------------------------------------
//
//          SpecialAttack.h
//
//  特殊攻撃の追尾、命中、寿命を管理する。
//-------------------------------------------------

#pragma once

#include <DirectXMath.h>

// ボスへ向かう追尾弾。シーンが位置を読んで軌跡演出を出す。
class SpecialAttack
{
public:
    bool Launch(const DirectX::XMFLOAT3& start,
        const DirectX::XMFLOAT3& target);
    // 命中したフレームに true を返す。寿命切れでは停止のみ。
    bool Update(float deltaTime, const DirectX::XMFLOAT3& target);
    bool IsActive() const { return m_active; }
    const DirectX::XMFLOAT3& GetPosition() const { return m_position; }
    const DirectX::XMFLOAT3& GetDirection() const { return m_direction; }

private:
    DirectX::XMFLOAT3 m_position {};
    DirectX::XMFLOAT3 m_direction { 0.0f, 0.0f, 1.0f };
    // 発射からの経過秒数。最大寿命を超えたら無効化する。
    float m_age = 0.0f;
    bool m_active = false;
};
