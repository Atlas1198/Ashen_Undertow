#pragma once

#include <DirectXMath.h>
#include "ComponentManager.h"

class Renderer;
class ModelManager;

// ゲームシーンに存在する実体の共通基底クラス。
class GameObject
{
public:
    GameObject() : m_componentManager(*this) {}
    virtual ~GameObject() = default;

    template<class T, class... Args>
    T* AddComponent(Args&&... args)
    {
        return m_componentManager.AddComponent<T>(std::forward<Args>(args)...);
    }

    template<class T>
    T* GetComponent() { return m_componentManager.GetComponent<T>(); }

    template<class T>
    const T* GetComponent() const { return m_componentManager.GetComponent<T>(); }

    void UpdateComponents(float deltaTime) { m_componentManager.Update(deltaTime); }
    void DrawComponents(Renderer& renderer) { m_componentManager.Draw(renderer); }

    template<class T>
    void CollectComponents(std::vector<T*>& result) { m_componentManager.CollectComponents(result); }

    template<class T>
    std::vector<T*> GetComponents() { return m_componentManager.GetComponents<T>(); }

    virtual bool Initialize(ModelManager& modelManager) = 0;
    virtual void Update(float deltaTime) { (void)deltaTime; }
    virtual void Draw(Renderer& renderer) { (void)renderer; }

    void SetPosition(float x, float y, float z) { m_position = { x, y, z }; }
    const DirectX::XMFLOAT3& GetPosition() const { return m_position; }

    DirectX::XMFLOAT4X4 CreateWorldMatrix() const
    {
        using namespace DirectX;
        const XMMATRIX world =
            XMMatrixScaling(m_scale.x, m_scale.y, m_scale.z) *
            XMMatrixRotationRollPitchYaw(m_rotation.x, m_rotation.y, m_rotation.z) *
            XMMatrixTranslation(m_position.x, m_position.y, m_position.z);

        XMFLOAT4X4 result;
        XMStoreFloat4x4(&result, world);
        return result;
    }

protected:
    DirectX::XMFLOAT3 m_position { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 m_rotation { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 m_scale { 1.0f, 1.0f, 1.0f };

private:
    ComponentManager m_componentManager;
};
