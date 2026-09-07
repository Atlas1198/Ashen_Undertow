#pragma once

#include <memory>
#include <vector>

class GameObject;
class Collider;

class CollisionManager
{
public:
    void Update(const std::vector<std::unique_ptr<GameObject>>& objects);

private:
    std::vector<Collider*> m_colliders;
};
