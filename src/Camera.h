#pragma once

#include <DirectXMath.h>

class Camera
{
public:
    void SetTarget(const DirectX::XMFLOAT3& target) { m_target = target; }
    void Rotate(float mouseX, float mouseY);
    const DirectX::XMFLOAT3& GetTarget() const { return m_target; }
    DirectX::XMFLOAT3 GetPosition() const;

private:
    DirectX::XMFLOAT3 m_target { 0, 0, 0 };
    float m_yaw = 0;
    float m_pitch = 0.39479112f;
    static constexpr float kDistance = 6.5f;
    static constexpr float kSensitivity = 0.005f;
};
