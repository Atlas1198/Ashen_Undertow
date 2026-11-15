//-------------------------------------------------
//
//          ComponentManager.h
//
//  一つの GameObject に属するコンポーネントを所有する。
//-------------------------------------------------

#pragma once

#include <memory>
#include <type_traits>
#include <utility>
#include <vector>
#include "Component.h"

// 1個のGameObjectに付くコンポーネントを所有する。
class ComponentManager
{
public:
    explicit ComponentManager(GameObject& owner) : m_owner(owner) {}
    ComponentManager(const ComponentManager&) = delete;
    ComponentManager& operator=(const ComponentManager&) = delete;

    template<class T, class... Args>
    T* AddComponent(Args&&... args)
    {
        static_assert(std::is_base_of_v<Component, T>, "T must inherit from Component");
        auto component = std::make_unique<T>(std::forward<Args>(args)...);
        static_cast<Component&>(*component).m_owner = &m_owner;
        if (!component->Initialize()) return nullptr;

        T* result = component.get();
        m_components.emplace_back(std::move(component));
        return result; // 非所有ポインタ。所有GameObjectの破棄まで有効。
    }

    // 同じ型が複数ある場合は、最初に登録されたものを返す。
    template<class T>
    T* GetComponent()
    {
        static_assert(std::is_base_of_v<Component, T>, "T must inherit from Component");
        for (const auto& component : m_components)
            if (auto* result = dynamic_cast<T*>(component.get())) return result;
        return nullptr;
    }

    template<class T>
    const T* GetComponent() const
    {
        static_assert(std::is_base_of_v<Component, T>, "T must inherit from Component");
        for (const auto& component : m_components)
            if (auto* result = dynamic_cast<const T*>(component.get())) return result;
        return nullptr;
    }

    void Update(float deltaTime);
    void Draw(Renderer& renderer);

    template<class T>
    void CollectComponents(std::vector<T*>& result)
    {
        static_assert(std::is_base_of_v<Component, T>, "T must inherit from Component");
        for (const auto& component : m_components)
            if (auto* match = dynamic_cast<T*>(component.get())) result.push_back(match);
    }

    template<class T>
    std::vector<T*> GetComponents()
    {
        static_assert(std::is_base_of_v<Component, T>, "T must inherit from Component");
        std::vector<T*> result;
        CollectComponents(result);
        return result;
    }

private:
    GameObject& m_owner;
    std::vector<std::unique_ptr<Component>> m_components;
};
