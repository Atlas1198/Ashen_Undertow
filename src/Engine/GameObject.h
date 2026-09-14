#pragma once

#include <DirectXMath.h>
#include <cstdint>
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

    int GetHP() const { return m_hp; }
    int GetMaxHP() const { return m_maxHP; }
    bool IsDead() const { return m_hp == 0; }

    void TakeDamage(int amount)
    {
        if (amount <= 0) return;
        m_hp -= amount < m_hp ? amount : m_hp;
    }

    // HPゼロからも回復可能。死亡時のゲーム処理は呼び出し側が決める。
    void Heal(int amount)
    {
        if (amount <= 0) return;
        const int missing = m_maxHP - m_hp;
        m_hp += amount < missing ? amount : missing;
    }

    void SetPosition(float x, float y, float z) { m_position = { x, y, z }; }
    const DirectX::XMFLOAT3& GetPosition() const { return m_position; }
    void SetRotation(float x, float y, float z) { m_rotation = { x, y, z }; }
    const DirectX::XMFLOAT3& GetRotation() const { return m_rotation; }

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

    // 通常は表示と同じ姿勢で影を作る。ビルボード等は個別に固定姿勢を返せる。
    virtual DirectX::XMFLOAT4X4 CreateShadowWorldMatrix() const
    {
        return CreateWorldMatrix();
    }
    virtual uint32_t GetRenderFlags() const { return 0; }

protected:
    // 各ObjectのInitializeで呼び、最大HPと現在HPを同時に設定する。
    void InitializeHP(int maxHP)
    {
        m_maxHP = maxHP > 0 ? maxHP : 0;
        m_hp = m_maxHP;
    }

    DirectX::XMFLOAT3 m_position { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 m_rotation { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 m_scale { 1.0f, 1.0f, 1.0f };

private:
    int m_maxHP = 1;
    int m_hp = 1;
    ComponentManager m_componentManager;
};
