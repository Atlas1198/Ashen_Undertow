#pragma once

#include <cmath>
#include <DirectXMath.h>
#include <vector>
#include "Component.h"

class CollisionManager;

// 軸平行な芯を半径分膨らませた形。球=点、カプセル=縦線分、箱=AABB。
struct CollisionShape
{
    DirectX::XMFLOAT3 center;
    DirectX::XMFLOAT3 halfExtents;
    float radius;
};

class Collider : public Component
{
public:
    virtual CollisionShape GetShape() const = 0;
    DirectX::XMFLOAT3 GetCenter() const;
    void SetOffset(float x, float y, float z) { m_offset = { x, y, z }; }

    // 最新のCollisionManager::Update結果。ポインタは所有オブジェクトの寿命内のみ有効。
    bool IsColliding() const { return IsEnabled() && !m_contacts.empty(); }
    const std::vector<Collider*>& GetContacts() const { return m_contacts; }
    static bool Intersects(const Collider& a, const Collider& b);

private:
    friend class CollisionManager;
    // サイズ・オフセットはワールド単位。描画の回転・拡縮からは独立。
    DirectX::XMFLOAT3 m_offset { 0, 0, 0 };
    std::vector<Collider*> m_contacts;
};

class SphereCollider final : public Collider
{
public:
    explicit SphereCollider(float radius = 0.5f) : m_radius(radius) {}
    bool Initialize() override { return std::isfinite(m_radius) && m_radius >= 0; }
    CollisionShape GetShape() const override
    {
        return { GetCenter(), { 0, 0, 0 }, m_radius };
    }

private:
    float m_radius;
};

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
    CollisionShape GetShape() const override
    {
        return { GetCenter(), m_halfExtents, 0 };
    }

private:
    DirectX::XMFLOAT3 m_halfExtents;
};
