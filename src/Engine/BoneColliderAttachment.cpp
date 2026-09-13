#include "Collider.h"

#include "AnimationComponent.h"
#include "GameObject.h"
#include "ModelComponent.h"

namespace
{
bool NodeWorldPosition(AnimationComponent& animation, ModelComponent& model,
    const char* nodeName, const DirectX::XMFLOAT3& localOffset,
    DirectX::XMFLOAT3& worldPosition)
{
    DirectX::XMFLOAT4X4 boneTransform;
    if (!animation.GetNodeModelTransform(nodeName, boneTransform)) return false;
    const auto modelWorld = model.CreateModelWorldMatrix();
    DirectX::XMStoreFloat3(&worldPosition, DirectX::XMVector3TransformCoord(
        DirectX::XMLoadFloat3(&localOffset),
        DirectX::XMLoadFloat4x4(&boneTransform) *
        DirectX::XMLoadFloat4x4(&modelWorld)));
    return true;
}
}

bool BoneColliderAttachment::Initialize()
{
    DirectX::XMFLOAT4X4 transform;
    return m_animation.GetNodeModelTransform(m_nodeName, transform);
}

void BoneColliderAttachment::Update(float deltaTime)
{
    (void)deltaTime;
    DirectX::XMFLOAT3 worldPosition;
    if (!NodeWorldPosition(m_animation, m_model, m_nodeName,
        m_localOffset, worldPosition)) return;
    const auto& ownerPosition = GetOwner().GetPosition();
    m_collider.SetOffset(worldPosition.x - ownerPosition.x,
        worldPosition.y - ownerPosition.y, worldPosition.z - ownerPosition.z);
}

bool BoneSegmentColliderAttachment::Initialize()
{
    DirectX::XMFLOAT4X4 transform;
    return m_animation.GetNodeModelTransform(m_startNode, transform) &&
        m_animation.GetNodeModelTransform(m_endNode, transform);
}

void BoneSegmentColliderAttachment::Update(float deltaTime)
{
    (void)deltaTime;
    DirectX::XMFLOAT3 start, end;
    if (!NodeWorldPosition(m_animation, m_model, m_startNode, m_startOffset, start) ||
        !NodeWorldPosition(m_animation, m_model, m_endNode, m_endOffset, end)) return;
    m_collider.SetEndpoints(start, end);
}
