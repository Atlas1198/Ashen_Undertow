#include "Boss.h"

#include "AnimationComponent.h"
#include "GameAssets.h"
#include "GameBalance.h"
#include "ModelComponent.h"

#include <algorithm>

using namespace GameBalance::Boss;

bool Boss::Initialize(ModelManager& modelManager)
{
    InitializeHP(5000);
    m_scale = { VisualScale, VisualScale, VisualScale };

    m_model = AddComponent<ModelComponent>(modelManager, GameAssets::EnemyModel);
    if (!m_model) return false;
    m_model->SetLocalRotation(0.0f, DirectX::XM_PI, 0.0f);

    m_animation = AddComponent<AnimationComponent>(modelManager, GameAssets::EnemyModel);
    return m_animation != nullptr;
}

DirectX::XMFLOAT3 Boss::GetSpecialAttackTarget() const
{
    DirectX::XMFLOAT4X4 headTransform;
    if (m_animation && m_model &&
        m_animation->GetNodeModelTransform("mixamorig:Head", headTransform))
    {
        const auto modelWorld = m_model->CreateModelWorldMatrix();
        DirectX::XMFLOAT3 position;
        DirectX::XMStoreFloat3(&position, DirectX::XMVector3TransformCoord(
            DirectX::XMVectorZero(), DirectX::XMLoadFloat4x4(&headTransform) *
            DirectX::XMLoadFloat4x4(&modelWorld)));
        return position;
    }
    return { m_position.x, m_position.y + m_scale.y * 0.78f, m_position.z };
}

void Boss::Update(float deltaTime)
{
    m_animation->Play("Idle", true, 1.0f, 0.2f);
    if (!IsDead())
        m_spawnTimer = std::max(0.0f, m_spawnTimer - deltaTime);
}

bool Boss::ConsumeSpawnRequest()
{
    if (IsDead() || m_spawnTimer > 0.0f) return false;
    m_spawnTimer = MiniCopySpawnInterval;
    return true;
}
