//-------------------------------------------------
//
//          CollisionResolver.h
//
//  接触した物体の重なりと速度を補正する。
//-------------------------------------------------

#pragma once

#include <memory>
#include <vector>

class Collider;
class GameObject;

// CollisionManagerが検出した接触を解決する。Triggerは接触情報だけを残す。
class CollisionResolver
{
public:
    void Update(const std::vector<std::unique_ptr<GameObject>>& objects);

private:
    std::vector<Collider*> m_colliders;
};
