//-------------------------------------------------
//
//          CollisionResolver.cpp
//
//  接触後に非 Trigger の重なりを位置補正する。
//-------------------------------------------------

#include "CollisionResolver.h"

#include "Collider.h"
#include "GameObject.h"
#include "Rigidbody.h"
#include <algorithm>
#include <cmath>
#include <functional>

using namespace DirectX;

namespace
{
// 中心間の分離量から押し戻す方向と深さを求める。
bool ComputePenetration(const CollisionShape& a, const CollisionShape& b,
    XMFLOAT3& normal, float& depth)
{
    const XMFLOAT3 delta {
        a.center.x - b.center.x,
        a.center.y - b.center.y,
        a.center.z - b.center.z
    };
    const XMFLOAT3 halfSum {
        a.halfExtents.x + b.halfExtents.x,
        a.halfExtents.y + b.halfExtents.y,
        a.halfExtents.z + b.halfExtents.z
    };
    const auto axisSeparation = [](float value, float half)
    {
        if (value > half) return value - half;
        if (value < -half) return value + half;
        return 0.0f;
    };
    const XMFLOAT3 separation {
        axisSeparation(delta.x, halfSum.x),
        axisSeparation(delta.y, halfSum.y),
        axisSeparation(delta.z, halfSum.z)
    };
    const float radius = a.radius + b.radius;
    const float distanceSquared = separation.x * separation.x +
        separation.y * separation.y + separation.z * separation.z;
    if (distanceSquared > 0.0000001f)
    {
        const float distance = std::sqrt(distanceSquared);
        depth = radius - distance;
        if (depth <= 0.0f) return false;
        normal = { separation.x / distance, separation.y / distance,
            separation.z / distance };
        return true;
    }

    // 中心が重なって方向を決められない場合は、最も浅い軸へ押し出す。
    const float overlaps[] = {
        halfSum.x + radius - std::abs(delta.x),
        halfSum.y + radius - std::abs(delta.y),
        halfSum.z + radius - std::abs(delta.z)
    };
    const int axis = overlaps[1] < overlaps[0]
        ? (overlaps[2] < overlaps[1] ? 2 : 1)
        : (overlaps[2] < overlaps[0] ? 2 : 0);
    depth = overlaps[axis];
    if (depth <= 0.0f) return false;
    normal = { 0.0f, 0.0f, 0.0f };
    (&normal.x)[axis] = (&delta.x)[axis] < 0.0f ? -1.0f : 1.0f;
    return true;
}

void MoveObject(GameObject& object, const XMFLOAT3& direction, float distance)
{
    const auto position = object.GetPosition();
    object.SetPosition(position.x + direction.x * distance,
        position.y + direction.y * distance,
        position.z + direction.z * distance);
}

// 接触面へ向かう速度成分だけ消し、接線方向の移動は残す。
void RemoveClosingVelocity(Rigidbody& body, const XMFLOAT3& outward)
{
    const auto velocity = body.GetVelocity();
    const float closing = velocity.x * outward.x + velocity.y * outward.y +
        velocity.z * outward.z;
    if (closing >= 0.0f) return;
    body.SetVelocity(velocity.x - outward.x * closing,
        velocity.y - outward.y * closing,
        velocity.z - outward.z * closing);
}
}

void CollisionResolver::Update(const std::vector<std::unique_ptr<GameObject>>& objects)
{
    m_colliders.clear();
    for (const auto& object : objects)
        object->CollectComponents(m_colliders);

    for (auto* a : m_colliders)
    {
        for (auto* b : a->GetContacts())
        {
            // 接触リストは双方にあるため、ポインタ順で各組を一度だけ処理する。
            if (!std::less<Collider*>{}(a, b)) continue;
            if (a->IsTrigger() || b->IsTrigger()) continue;

            XMFLOAT3 normal;
            float depth = 0.0f;
            if (!ComputePenetration(a->GetShape(), b->GetShape(), normal, depth))
                continue;

            auto* bodyA = a->GetOwner().GetComponent<Rigidbody>();
            auto* bodyB = b->GetOwner().GetComponent<Rigidbody>();
            // 重い側ほど小さく移動する。静的／運動学的な側は動かさない。
            const float inverseMassA = bodyA && !bodyA->IsKinematic()
                ? 1.0f / bodyA->GetMass() : 0.0f;
            const float inverseMassB = bodyB && !bodyB->IsKinematic()
                ? 1.0f / bodyB->GetMass() : 0.0f;
            const float inverseMassSum = inverseMassA + inverseMassB;
            if (inverseMassSum <= 0.0f) continue;

            if (inverseMassA > 0.0f)
            {
                MoveObject(a->GetOwner(), normal, depth * inverseMassA / inverseMassSum);
                RemoveClosingVelocity(*bodyA, normal);
            }
            if (inverseMassB > 0.0f)
            {
                const XMFLOAT3 outward { -normal.x, -normal.y, -normal.z };
                MoveObject(b->GetOwner(), outward, depth * inverseMassB / inverseMassSum);
                RemoveClosingVelocity(*bodyB, outward);
            }
        }
    }
}
