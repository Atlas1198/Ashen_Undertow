#pragma once

#include <cmath>
#include "Collider.h"

class SphereCollider final : public Collider
{
public:
    explicit SphereCollider(float radius = 0.5f) : m_radius(radius) {}
    bool Initialize() override { return std::isfinite(m_radius) && m_radius >= 0; }
    CollisionShape GetShape() const override { return { GetCenter(), { 0, 0, 0 }, m_radius }; }

private:
    float m_radius;
};
