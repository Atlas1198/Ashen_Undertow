#include "Collider.h"
#include "GameObject.h"
#include <cmath>

DirectX::XMFLOAT3 Collider::GetCenter() const
{
    const auto& p = GetOwner().GetPosition();
    return { p.x + m_offset.x, p.y + m_offset.y, p.z + m_offset.z };
}

bool Collider::HasContactWith(const GameObject& object) const
{
    for (const auto* contact : m_contacts)
        if (&contact->GetOwner() == &object) return true;
    return false;
}

bool Collider::HasContactWith(const GameObject& object, ColliderRole role) const
{
    for (const auto* contact : m_contacts)
        if (&contact->GetOwner() == &object && contact->GetRole() == role) return true;
    return false;
}

bool Collider::Intersects(const Collider& a, const Collider& b)
{
    if (!a.IsEnabled() || !b.IsEnabled()) return false;
    const auto sa = a.GetShape();
    const auto sb = b.GetShape();
    // 軸ごとの区間距離から芯同士の最短距離を求める。
    // この3形状では「芯の距離 <= 半径の和」が全6組み合わせで成立する。
    const auto gap = [](float a, float b, float ha, float hb)
    {
        const double distance = std::abs(double(a) - b) - double(ha) - hb;
        return distance > 0.0 ? distance : 0.0;
    };
    if (!std::isfinite(sa.center.x) || !std::isfinite(sa.center.y) ||
        !std::isfinite(sa.center.z) || !std::isfinite(sb.center.x) ||
        !std::isfinite(sb.center.y) || !std::isfinite(sb.center.z)) return false;
    const double dx = gap(sa.center.x, sb.center.x, sa.halfExtents.x, sb.halfExtents.x);
    const double dy = gap(sa.center.y, sb.center.y, sa.halfExtents.y, sb.halfExtents.y);
    const double dz = gap(sa.center.z, sb.center.z, sa.halfExtents.z, sb.halfExtents.z);
    const double radius = double(sa.radius) + sb.radius;
    return dx * dx + dy * dy + dz * dz <= radius * radius;
}
