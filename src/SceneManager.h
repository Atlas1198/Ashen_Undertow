#pragma once

#include "ObjectManager.h"

class ModelManager;
class Renderer;

// 各シーンが共通して実装するインターフェース。
class SceneManager
{
public:
    explicit SceneManager(ModelManager& modelManager)
        : m_objectManager(modelManager)
    {
    }

    virtual ~SceneManager() = default;

    virtual bool Initialize() = 0;

    void Update(float deltaTime)
    {
        OnUpdate(deltaTime);
        m_objectManager.Update(deltaTime);
    }

    void Draw(Renderer& renderer)
    {
        OnDraw(renderer);
        m_objectManager.Draw(renderer);
    }

protected:
    template<class T, class... Args>
    T* AddGameObject(Args&&... args)
    {
        return m_objectManager.AddGameObject<T>(std::forward<Args>(args)...);
    }

    virtual void OnUpdate(float deltaTime) { (void)deltaTime; }
    virtual void OnDraw(Renderer& renderer) { (void)renderer; }

private:
    ObjectManager m_objectManager;
};
