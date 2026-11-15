//-------------------------------------------------
//
//          SceneManager.h
//
//  シーンの更新順と画面遷移の共通インターフェースを定義する。
//-------------------------------------------------

#pragma once

#include "ObjectManager.h"

class ModelManager;
class Renderer;
class UIManager;

// シーンが Manager に渡す一回限りの遷移要求。
enum class SceneRequest { None, Game, Tutorial, Title, Retry };

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
    // 読み取りと同時にクリアし、次フレームへの持ち越しを防ぐ。
    SceneRequest ConsumeSceneRequest()
    {
        const auto request = m_sceneRequest;
        m_sceneRequest = SceneRequest::None;
        return request;
    }

    // シーンの事前処理、オブジェクト更新、接触結果の事後処理の順。
    void Update(float deltaTime)
    {
        OnUpdate(deltaTime);
        if (IsSimulationPaused()) return;
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
    virtual bool IsSimulationPaused() const { return false; }
    virtual void OnDraw(Renderer& renderer) { (void)renderer; }
    virtual void OnDrawUI(UIManager&) {}
    void RequestScene(SceneRequest request) { m_sceneRequest = request; }

private:
    // オブジェクトはシーンに属し、シーン破棄時にまとめて解放される。
    ObjectManager m_objectManager;
    SceneRequest m_sceneRequest = SceneRequest::None;
};
