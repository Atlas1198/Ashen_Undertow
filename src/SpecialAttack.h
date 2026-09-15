#pragma once

#include <DirectXMath.h>

class SpecialAttack
{
public:
    bool Launch(const DirectX::XMFLOAT3& start,
        const DirectX::XMFLOAT3& target);
    bool Update(float deltaTime, const DirectX::XMFLOAT3& target);
    bool IsActive() const { return m_active; }
    const DirectX::XMFLOAT3& GetPosition() const { return m_position; }
    const DirectX::XMFLOAT3& GetDirection() const { return m_direction; }

private:
    DirectX::XMFLOAT3 m_position {};
    DirectX::XMFLOAT3 m_direction { 0.0f, 0.0f, 1.0f };
    float m_age = 0.0f;
    bool m_active = false;
};
