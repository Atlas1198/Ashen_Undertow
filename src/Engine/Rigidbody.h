#pragma once

#include <DirectXMath.h>
#include "Component.h"

// 力と速度を積分し、所有GameObjectの位置を更新する。
// Colliderによる接触検出・押し戻しは別処理として扱う。
class Rigidbody : public Component
{
public:
    void Update(float deltaTime) override;

    void SetVelocity(float x, float y, float z) { m_velocity = { x, y, z }; }
    const DirectX::XMFLOAT3& GetVelocity() const { return m_velocity; }

    void AddForce(float x, float y, float z);
    void AddImpulse(float x, float y, float z);
    void ClearForces() { m_force = { 0.0f, 0.0f, 0.0f }; }

    void SetMass(float mass);
    float GetMass() const { return m_mass; }
    void SetLinearDrag(float drag);
    float GetLinearDrag() const { return m_linearDrag; }

    void SetUseGravity(bool useGravity) { m_useGravity = useGravity; }
    bool UsesGravity() const { return m_useGravity; }
    void SetGravity(float x, float y, float z) { m_gravity = { x, y, z }; }
    const DirectX::XMFLOAT3& GetGravity() const { return m_gravity; }

    void SetKinematic(bool kinematic);
    bool IsKinematic() const { return m_isKinematic; }

private:
    DirectX::XMFLOAT3 m_velocity { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 m_force { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 m_gravity { 0.0f, -9.81f, 0.0f };
    float m_mass = 1.0f;
    float m_linearDrag = 0.0f;
    bool m_useGravity = true;
    bool m_isKinematic = false;
};
