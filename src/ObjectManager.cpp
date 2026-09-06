#include "ObjectManager.h"

void ObjectManager::Update(float deltaTime)
{
    for (const auto& gameObject : m_gameObjects)
        gameObject->Update(deltaTime);
}

void ObjectManager::Draw(Renderer& renderer)
{
    for (const auto& gameObject : m_gameObjects)
        gameObject->Draw(renderer);
}
