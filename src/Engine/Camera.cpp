#include "Camera.h"
#include <algorithm>
#include <cmath>

void Camera::Rotate(float mouseX, float mouseY)
{
    m_yaw = std::remainder(m_yaw - mouseX * kSensitivity, DirectX::XM_2PI);
    constexpr float limit = DirectX::XM_PIDIV2 - 0.05f;
    m_pitch = std::clamp(m_pitch + mouseY * kSensitivity, -limit, limit);
}

DirectX::XMFLOAT3 Camera::GetPosition() const
{
    const float horizontal = kDistance * std::cos(m_pitch);
    return { m_target.x + horizontal * std::sin(m_yaw),
        m_target.y + kDistance * std::sin(m_pitch),
        m_target.z - horizontal * std::cos(m_yaw) };
}
