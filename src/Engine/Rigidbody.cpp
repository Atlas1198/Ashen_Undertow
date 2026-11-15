//-------------------------------------------------
//
//          Rigidbody.cpp
//
//  速度・力・重力を積分して GameObject の位置を進める。
//-------------------------------------------------

#include "Rigidbody.h"

#include "GameObject.h"
#include <algorithm>

// 力から加速度を求め、抗力と重力を反映してから位置へ積分する。
void Rigidbody::Update(float deltaTime)
{
    if (deltaTime <= 0.0f) return;

    if (m_isKinematic)
    {
        ClearForces();
        return;
    }

    const float inverseMass = 1.0f / m_mass;
    const float gravityX = m_useGravity ? m_gravity.x : 0.0f;
    const float gravityY = m_useGravity ? m_gravity.y : 0.0f;
    const float gravityZ = m_useGravity ? m_gravity.z : 0.0f;
    m_velocity.x += (m_force.x * inverseMass + gravityX) * deltaTime;
    m_velocity.y += (m_force.y * inverseMass + gravityY) * deltaTime;
    m_velocity.z += (m_force.z * inverseMass + gravityZ) * deltaTime;

    const float damping = 1.0f / (1.0f + m_linearDrag * deltaTime);
    m_velocity.x *= damping;
    m_velocity.y *= damping;
    m_velocity.z *= damping;

    const auto position = GetOwner().GetPosition();
    GetOwner().SetPosition(
        position.x + m_velocity.x * deltaTime,
        position.y + m_velocity.y * deltaTime,
        position.z + m_velocity.z * deltaTime);
    ClearForces();
}

void Rigidbody::AddForce(float x, float y, float z)
{
    m_force.x += x;
    m_force.y += y;
    m_force.z += z;
}

// 瞬間的な運動量を質量で割って速度へ直接加える。
void Rigidbody::AddImpulse(float x, float y, float z)
{
    if (m_isKinematic) return;
    const float inverseMass = 1.0f / m_mass;
    m_velocity.x += x * inverseMass;
    m_velocity.y += y * inverseMass;
    m_velocity.z += z * inverseMass;
}

void Rigidbody::SetMass(float mass)
{
    m_mass = std::max(mass, 0.0001f);
}

void Rigidbody::SetLinearDrag(float drag)
{
    m_linearDrag = std::max(drag, 0.0f);
}

void Rigidbody::SetKinematic(bool kinematic)
{
    m_isKinematic = kinematic;
    if (kinematic)
    {
        m_velocity = { 0.0f, 0.0f, 0.0f };
        ClearForces();
    }
}
