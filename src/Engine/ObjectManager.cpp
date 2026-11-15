//-------------------------------------------------
//
//          ObjectManager.cpp
//
//  シーン内の実体と部品を更新し、衝突を解決する。
//-------------------------------------------------

#include "ObjectManager.h"

void ObjectManager::Update(float deltaTime)
{
    const auto count = m_gameObjects.size();
    for (size_t i = 0; i < count; ++i)
        m_gameObjects[i]->Update(deltaTime);

    // 全オブジェクトの位置更新後に、追加された機能を更新する。
    for (size_t i = 0; i < count; ++i)
        m_gameObjects[i]->UpdateComponents(deltaTime);

    m_collisionManager.Update(m_gameObjects);
    m_collisionResolver.Update(m_gameObjects);
}

void ObjectManager::Draw(Renderer& renderer)
{
    const auto count = m_gameObjects.size();
    for (size_t i = 0; i < count; ++i)
    {
        auto* object = m_gameObjects[i].get();
        object->Draw(renderer);
        object->DrawComponents(renderer);
    }
}
