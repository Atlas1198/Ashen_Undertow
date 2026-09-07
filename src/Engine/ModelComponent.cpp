#include "ModelComponent.h"
#include "ModelManager.h"
#include "GameObject.h"
#include "Renderer.h"

bool ModelComponent::Initialize()
{
    m_model = m_models.Load(m_path);
    return m_model.IsValid();
}

void ModelComponent::Draw(Renderer& renderer)
{
    renderer.DrawModel(m_model, GetOwner().CreateWorldMatrix());
}
