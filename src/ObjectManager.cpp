#include "ObjectManager.h"

void ObjectManager::Update(float deltaTime)
{
    for (const auto& gameObject : m_gameObjects)
        gameObject->Update(deltaTime);

    // 全オブジェクトの位置更新後に、追加された機能を更新する。
    for (const auto& gameObject : m_gameObjects)
        gameObject->UpdateComponents(deltaTime);
}

void ObjectManager::Draw(Renderer& renderer)
{
    for (const auto& gameObject : m_gameObjects)
        gameObject->Draw(renderer);
}
