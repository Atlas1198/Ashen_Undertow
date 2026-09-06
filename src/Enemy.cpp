#include "Enemy.h"

#include "Renderer.h"

bool Enemy::Initialize(ModelManager& modelManager)
{
    m_modelHandle = modelManager.Load("assets/model.glb");
    return m_modelHandle.IsValid();
}

void Enemy::Update(float deltaTime)
{
    (void)deltaTime;
}

void Enemy::Draw(Renderer& renderer)
{
    renderer.DrawModel(m_modelHandle, CreateWorldMatrix());
}
