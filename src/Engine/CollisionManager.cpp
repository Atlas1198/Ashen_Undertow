//-------------------------------------------------
//
//          CollisionManager.cpp
//
//  シーン内の Collider の接触情報を毎フレーム作り直す。
//-------------------------------------------------

#include "CollisionManager.h"
#include "GameObject.h"
#include "Collider.h"

void CollisionManager::Update(const std::vector<std::unique_ptr<GameObject>>& objects)
{
    m_colliders.clear();
    for (const auto& object : objects)
        object->CollectComponents(m_colliders);

    for (auto* collider : m_colliders)
        collider->m_contacts.clear();

    for (size_t i = 0; i < m_colliders.size(); ++i)
    {
        for (size_t j = i + 1; j < m_colliders.size(); ++j)
        {
            auto& a = *m_colliders[i];
            auto& b = *m_colliders[j];
            if (&a.GetOwner() == &b.GetOwner()) continue;
            if (!Collider::Intersects(a, b)) continue;
            a.m_contacts.push_back(&b);
            b.m_contacts.push_back(&a);
        }
    }
}
