//-------------------------------------------------
//
//          CollisionManager.h
//
//  シーン内の衝突判定と接触リストの更新を管理する。
//-------------------------------------------------

#pragma once

#include <memory>
#include <vector>

class GameObject;
class Collider;

// 有効な Collider の組を調べ、双方の接触リストを毎フレーム更新する。
class CollisionManager
{
public:
    void Update(const std::vector<std::unique_ptr<GameObject>>& objects);

private:
    // 更新中だけ使う非所有ポインタ。GameObject 側が実体を保持する。
    std::vector<Collider*> m_colliders;
};
