#pragma once

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
