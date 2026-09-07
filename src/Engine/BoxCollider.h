#pragma once

#include <cmath>
#include "Collider.h"

// 回転しないAABB。引数は各軸の半サイズ。
class BoxCollider final : public Collider
{
public:
    explicit BoxCollider(float halfX = 0.5f, float halfY = 0.5f, float halfZ = 0.5f)
        : m_halfExtents(halfX, halfY, halfZ) {}
    bool Initialize() override
    {
        return std::isfinite(m_halfExtents.x) && m_halfExtents.x >= 0 &&
            std::isfinite(m_halfExtents.y) && m_halfExtents.y >= 0 &&
            std::isfinite(m_halfExtents.z) && m_halfExtents.z >= 0;
    }
    CollisionShape GetShape() const override { return { GetCenter(), m_halfExtents, 0 }; }

private:
    DirectX::XMFLOAT3 m_halfExtents;
};
