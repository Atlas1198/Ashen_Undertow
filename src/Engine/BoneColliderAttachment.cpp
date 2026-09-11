#include "Collider.h"

#include "AnimationComponent.h"
#include "GameObject.h"
#include "ModelComponent.h"

bool BoneColliderAttachment::Initialize()
{
    DirectX::XMFLOAT4X4 transform;
    return m_animation.GetNodeModelTransform(m_nodeName, transform);
}

void BoneColliderAttachment::Update(float deltaTime)
{
    (void)deltaTime;
    DirectX::XMFLOAT4X4 boneTransform;
    if (!m_animation.GetNodeModelTransform(m_nodeName, boneTransform)) return;
    const auto modelWorld = m_model.CreateModelWorldMatrix();
    DirectX::XMFLOAT3 worldPosition;
    DirectX::XMStoreFloat3(&worldPosition, DirectX::XMVector3TransformCoord(
        DirectX::XMLoadFloat3(&m_localOffset),
        DirectX::XMLoadFloat4x4(&boneTransform) *
        DirectX::XMLoadFloat4x4(&modelWorld)));
    const auto& ownerPosition = GetOwner().GetPosition();
    m_collider.SetOffset(worldPosition.x - ownerPosition.x,
        worldPosition.y - ownerPosition.y, worldPosition.z - ownerPosition.z);
}
