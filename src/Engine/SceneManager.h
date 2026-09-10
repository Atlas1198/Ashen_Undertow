#pragma once

#include "ObjectManager.h"

class ModelManager;
class Renderer;
class UIManager;

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
    virtual bool InitializeUI(UIManager&) { return true; }
    void DrawUI(UIManager& ui) { OnDrawUI(ui); }

    void Update(float deltaTime)
    {
        OnUpdate(deltaTime);
        m_objectManager.Update(deltaTime);
        OnAfterUpdate(deltaTime);
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
    virtual void OnAfterUpdate(float deltaTime) { (void)deltaTime; }
    virtual void OnDraw(Renderer& renderer) { (void)renderer; }
    virtual void OnDrawUI(UIManager&) {}

private:
    ObjectManager m_objectManager;
};
