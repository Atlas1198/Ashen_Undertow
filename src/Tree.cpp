//-------------------------------------------------
//
//          Tree.cpp
//
//  樹木オブジェクトのモデルと配置を設定する。
//-------------------------------------------------

#include "Tree.h"

#include "GameAssets.h"
#include "ModelComponent.h"

#include <cmath>

bool Tree::Initialize(ModelManager& modelManager)
{
    return AddComponent<ModelComponent>(modelManager, GameAssets::TreeModel) != nullptr;
}

void Tree::FaceCamera(const DirectX::XMFLOAT3& cameraPosition)
{
    const float toCameraX = cameraPosition.x - m_position.x;
    const float toCameraZ = cameraPosition.z - m_position.z;
    m_rotation.y = std::atan2(toCameraX, toCameraZ);
}

DirectX::XMFLOAT4X4 Tree::CreateShadowWorldMatrix() const
{
    // 木の板を太陽へ正対させて固定し、カメラ回転で影が動かないようにする。
    constexpr float ShadowRotationY = 0.94f;
    DirectX::XMFLOAT4X4 result;
    DirectX::XMStoreFloat4x4(&result,
        DirectX::XMMatrixScaling(m_scale.x, m_scale.y, m_scale.z) *
        DirectX::XMMatrixRotationY(ShadowRotationY) *
        DirectX::XMMatrixTranslation(m_position.x, m_position.y, m_position.z));
    return result;
}
