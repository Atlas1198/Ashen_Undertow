#include "Collider.h"
#include "GameObject.h"
#include <algorithm>
#include <cmath>

namespace
{
double ClosestSegmentDistanceSquared(const CollisionShape& a,
    const CollisionShape& b)
{
    using namespace DirectX;
    const XMVECTOR p1 = XMLoadFloat3(&a.segmentStart);
    const XMVECTOR q1 = XMLoadFloat3(&a.segmentEnd);
    const XMVECTOR p2 = XMLoadFloat3(&b.segmentStart);
    const XMVECTOR q2 = XMLoadFloat3(&b.segmentEnd);
    const XMVECTOR d1 = q1 - p1;
    const XMVECTOR d2 = q2 - p2;
    const XMVECTOR r = p1 - p2;
    const float aa = XMVectorGetX(XMVector3Dot(d1, d1));
    const float ee = XMVectorGetX(XMVector3Dot(d2, d2));
    const float f = XMVectorGetX(XMVector3Dot(d2, r));
    float s = 0.0f, t = 0.0f;
    if (aa <= 0.000001f && ee <= 0.000001f)
        return XMVectorGetX(XMVector3LengthSq(r));
    if (aa <= 0.000001f)
        t = std::clamp(f / ee, 0.0f, 1.0f);
    else
    {
        const float c = XMVectorGetX(XMVector3Dot(d1, r));
        if (ee <= 0.000001f)
            s = std::clamp(-c / aa, 0.0f, 1.0f);
        else
        {
            const float bDot = XMVectorGetX(XMVector3Dot(d1, d2));
            const float denominator = aa * ee - bDot * bDot;
            if (denominator != 0.0f)
                s = std::clamp((bDot * f - c * ee) / denominator, 0.0f, 1.0f);
            t = (bDot * s + f) / ee;
            if (t < 0.0f) { t = 0.0f; s = std::clamp(-c / aa, 0.0f, 1.0f); }
            else if (t > 1.0f)
            {
                t = 1.0f;
                s = std::clamp((bDot - c) / aa, 0.0f, 1.0f);
            }
        }
    }
    return XMVectorGetX(XMVector3LengthSq((p1 + d1 * s) - (p2 + d2 * t)));
}

CollisionShape AsSegment(CollisionShape shape)
{
    if (shape.hasSegment) return shape;
    shape.segmentStart = { shape.center.x,
        shape.center.y - shape.halfExtents.y, shape.center.z };
    shape.segmentEnd = { shape.center.x,
        shape.center.y + shape.halfExtents.y, shape.center.z };
    shape.hasSegment = true;
    return shape;
}
}

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

void SegmentCollider::SetEndpoints(const DirectX::XMFLOAT3& start,
    const DirectX::XMFLOAT3& end)
{
    const auto& position = GetOwner().GetPosition();
    m_startOffset = { start.x - position.x, start.y - position.y, start.z - position.z };
    m_endOffset = { end.x - position.x, end.y - position.y, end.z - position.z };
}

CollisionShape SegmentCollider::GetShape() const
{
    const auto& position = GetOwner().GetPosition();
    const DirectX::XMFLOAT3 start { position.x + m_startOffset.x,
        position.y + m_startOffset.y, position.z + m_startOffset.z };
    const DirectX::XMFLOAT3 end { position.x + m_endOffset.x,
        position.y + m_endOffset.y, position.z + m_endOffset.z };
    const DirectX::XMFLOAT3 center { (start.x + end.x) * 0.5f,
        (start.y + end.y) * 0.5f, (start.z + end.z) * 0.5f };
    return { center,
        { std::abs(end.x - start.x) * 0.5f,
          std::abs(end.y - start.y) * 0.5f,
          std::abs(end.z - start.z) * 0.5f },
        m_radius, start, end, true };
}

bool Collider::Intersects(const Collider& a, const Collider& b)
{
    if (!a.IsEnabled() || !b.IsEnabled()) return false;
    const auto sa = a.GetShape();
    const auto sb = b.GetShape();
    const bool aRounded = sa.radius > 0.0f;
    const bool bRounded = sb.radius > 0.0f;
    if ((sa.hasSegment || sb.hasSegment) && aRounded && bRounded)
    {
        const auto segmentA = AsSegment(sa);
        const auto segmentB = AsSegment(sb);
        const double radius = double(sa.radius) + sb.radius;
        return ClosestSegmentDistanceSquared(segmentA, segmentB) <= radius * radius;
    }
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
