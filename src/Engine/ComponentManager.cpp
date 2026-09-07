#include "ComponentManager.h"

void ComponentManager::Draw(Renderer& renderer)
{
    const auto count = m_components.size();
    for (size_t i = 0; i < count; ++i)
    {
        Component* component = m_components[i].get();
        if (component->IsEnabled()) component->Draw(renderer);
    }
}

void ComponentManager::Update(float deltaTime)
{
    // Update内で追加されても反復子を無効化しない。新規分は次回から更新する。
    const auto count = m_components.size();
    for (size_t i = 0; i < count; ++i)
    {
        Component* component = m_components[i].get();
        if (component->IsEnabled()) component->Update(deltaTime);
    }
}
