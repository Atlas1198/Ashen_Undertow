#pragma once

#include <cmath>
#include <DirectXMath.h>
#include <vector>
#include "Component.h"

class CollisionManager;
class AnimationComponent;
class ModelComponent;

enum class ColliderRole
{
    Body,
    Hurtbox,
    Hitbox
};

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
    void SetTrigger(bool trigger) { m_isTrigger = trigger; }
    bool IsTrigger() const { return m_isTrigger; }
    void SetRole(ColliderRole role) { m_role = role; }
    ColliderRole GetRole() const { return m_role; }

    // 最新のCollisionManager::Update結果。ポインタは所有オブジェクトの寿命内のみ有効。
    bool IsColliding() const { return IsEnabled() && !m_contacts.empty(); }
    bool IsTriggering() const { return m_isTrigger && IsColliding(); }
    bool HasContactWith(const GameObject& object) const;
    bool HasContactWith(const GameObject& object, ColliderRole role) const;
    const std::vector<Collider*>& GetContacts() const { return m_contacts; }
    static bool Intersects(const Collider& a, const Collider& b);

private:
    friend class CollisionManager;
    // サイズ・オフセットはワールド単位。描画の回転・拡縮からは独立。
    DirectX::XMFLOAT3 m_offset { 0, 0, 0 };
    bool m_isTrigger = false;
    ColliderRole m_role = ColliderRole::Body;
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

// アニメーション済みボーンへCollider中心を追従させる。
class BoneColliderAttachment final : public Component
{
public:
    BoneColliderAttachment(AnimationComponent& animation, ModelComponent& model,
        Collider& collider, const char* nodeName,
        DirectX::XMFLOAT3 localOffset = { 0, 0, 0 })
        : m_animation(animation), m_model(model), m_collider(collider),
          m_nodeName(nodeName), m_localOffset(localOffset) {}
    bool Initialize() override;
    void Update(float deltaTime) override;

private:
    AnimationComponent& m_animation;
    ModelComponent& m_model;
    Collider& m_collider;
    const char* m_nodeName;
    DirectX::XMFLOAT3 m_localOffset;
};
