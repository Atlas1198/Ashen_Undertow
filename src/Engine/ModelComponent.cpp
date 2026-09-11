#include "ModelComponent.h"
#include "AnimationComponent.h"
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
    const auto* animation = GetOwner().GetComponent<AnimationComponent>();
    const auto modelWorld = CreateModelWorldMatrix();
    renderer.DrawModel(m_model, modelWorld,
        animation ? &animation->GetSkinMatrices() : nullptr);
}

DirectX::XMFLOAT4X4 ModelComponent::CreateModelWorldMatrix() const
{
    const auto ownerWorld = GetOwner().CreateWorldMatrix();
    DirectX::XMFLOAT4X4 result;
    DirectX::XMStoreFloat4x4(&result,
        DirectX::XMMatrixRotationRollPitchYaw(
            m_localRotation.x, m_localRotation.y, m_localRotation.z) *
        DirectX::XMMatrixTranslation(m_offset.x, m_offset.y, m_offset.z) *
        DirectX::XMLoadFloat4x4(&ownerWorld));
    return result;
}
