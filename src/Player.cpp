#include "Player.h"

#include "Renderer.h"

bool Player::Initialize(ModelManager& modelManager)
{
    m_modelHandle = modelManager.Load("assets/model.glb");
    return m_modelHandle.IsValid();
}

void Player::Update()
{
}

void Player::Draw(Renderer& renderer)
{
    renderer.DrawModel(m_modelHandle, CreateWorldMatrix());
}
