//-------------------------------------------------
//
//          SpecialAttack.cpp
//
//  特殊攻撃の追尾移動と命中・寿命判定を扱う。
//-------------------------------------------------

#include "SpecialAttack.h"

#include "GameBalance.h"

#include <algorithm>
#include <cmath>

namespace
{
DirectX::XMFLOAT3 DirectionTo(const DirectX::XMFLOAT3& from,
    const DirectX::XMFLOAT3& to)
{
    const float x = to.x - from.x;
    const float y = to.y - from.y;
    const float z = to.z - from.z;
    const float length = std::sqrt(x * x + y * y + z * z);
    if (length <= 0.0001f) return { 0.0f, 0.0f, 1.0f };
    return { x / length, y / length, z / length };
}
}

bool SpecialAttack::Launch(const DirectX::XMFLOAT3& start,
    const DirectX::XMFLOAT3& target)
{
    if (m_active) return false;
    m_position = start;
    m_direction = DirectionTo(start, target);
    m_age = 0.0f;
    m_active = true;
    return true;
}

bool SpecialAttack::Update(float deltaTime, const DirectX::XMFLOAT3& target)
{
    if (!m_active || deltaTime <= 0.0f) return false;

    using namespace GameBalance::SpecialAttack;
    const auto desired = DirectionTo(m_position, target);
    const float blend = 1.0f - std::exp(-HomingResponsiveness * deltaTime);
    m_direction.x += (desired.x - m_direction.x) * blend;
    m_direction.y += (desired.y - m_direction.y) * blend;
    m_direction.z += (desired.z - m_direction.z) * blend;
    m_direction = DirectionTo({}, m_direction);
    m_position.x += m_direction.x * Speed * deltaTime;
    m_position.y += m_direction.y * Speed * deltaTime;
    m_position.z += m_direction.z * Speed * deltaTime;
    m_age += deltaTime;

    const float dx = target.x - m_position.x;
    const float dy = target.y - m_position.y;
    const float dz = target.z - m_position.z;
    const bool hit = dx * dx + dy * dy + dz * dz <= HitRadius * HitRadius;
    if (hit || m_age >= MaximumLifetime) m_active = false;
    return hit;
}
