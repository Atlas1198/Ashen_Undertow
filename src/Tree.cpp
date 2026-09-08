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
