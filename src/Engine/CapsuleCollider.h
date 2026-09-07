#pragma once

#include <cmath>
#include "Collider.h"

// Y軸方向のカプセル。全高 = 2 * (halfSegmentHeight + radius)。
class CapsuleCollider final : public Collider
{
public:
    explicit CapsuleCollider(float radius = 0.5f, float halfSegmentHeight = 0.5f)
        : m_radius(radius), m_halfSegmentHeight(halfSegmentHeight) {}
    bool Initialize() override
    {
        return std::isfinite(m_radius) && m_radius >= 0 &&
            std::isfinite(m_halfSegmentHeight) && m_halfSegmentHeight >= 0;
    }
    CollisionShape GetShape() const override
    {
        return { GetCenter(), { 0, m_halfSegmentHeight, 0 }, m_radius };
    }

private:
    float m_radius;
    float m_halfSegmentHeight;
};
