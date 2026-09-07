#pragma once

#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include "GameObject.h"
#include "CollisionManager.h"

class ModelManager;
class Renderer;

// 1シーン内のGameObjectを生成・所有し、更新と描画を一括実行する。
class ObjectManager
{
public:
    explicit ObjectManager(ModelManager& modelManager)
        : m_modelManager(modelManager)
    {
    }

    template<class T, class... Args>
    T* AddGameObject(Args&&... args)
    {
        static_assert(std::is_base_of_v<GameObject, T>,
            "T must inherit from GameObject");

        auto object = std::make_unique<T>(std::forward<Args>(args)...);
        if (!object->Initialize(m_modelManager))
            return nullptr;

        T* result = object.get();
        m_gameObjects.emplace_back(std::move(object));
        return result;
    }

    void Update(float deltaTime);
    void Draw(Renderer& renderer);

private:
    ModelManager& m_modelManager;
    std::vector<std::unique_ptr<GameObject>> m_gameObjects;
    CollisionManager m_collisionManager;
};
