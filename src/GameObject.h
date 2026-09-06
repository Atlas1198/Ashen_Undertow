#pragma once

#include <DirectXMath.h>

class Renderer;

// ゲームシーンに存在する実体の共通基底クラス。
class GameObject
{
public:
    virtual ~GameObject() = default;

    virtual void Update() = 0;
    virtual void Draw(Renderer& renderer) = 0;

    void SetPosition(float x, float y, float z) { m_position = { x, y, z }; }

protected:
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

    DirectX::XMFLOAT3 m_position { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 m_rotation { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 m_scale { 1.0f, 1.0f, 1.0f };
};
