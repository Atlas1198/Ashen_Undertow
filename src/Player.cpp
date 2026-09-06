#include "Player.h"

#include <Windows.h>
#include <cmath>

#include "Renderer.h"

bool Player::Initialize(ModelManager& modelManager)
{
    m_modelHandle = modelManager.Load("assets/model.glb");
    return m_modelHandle.IsValid();
}

void Player::Update(float deltaTime)
{
    float moveX = 0.0f;
    float moveZ = 0.0f;

    if (GetAsyncKeyState('W') & 0x8000) moveZ += 1.0f;
    if (GetAsyncKeyState('S') & 0x8000) moveZ -= 1.0f;
    if (GetAsyncKeyState('A') & 0x8000) moveX -= 1.0f;
    if (GetAsyncKeyState('D') & 0x8000) moveX += 1.0f;

    const float length = std::sqrt(moveX * moveX + moveZ * moveZ);
    if (length > 0.0f)
    {
        constexpr float moveSpeed = 3.0f;
        m_position.x += moveX / length * moveSpeed * deltaTime;
        m_position.z += moveZ / length * moveSpeed * deltaTime;
    }
}

void Player::Draw(Renderer& renderer)
{
    renderer.DrawModel(m_modelHandle, CreateWorldMatrix());
}
