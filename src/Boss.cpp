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

    auto* model = AddComponent<ModelComponent>(modelManager, GameAssets::EnemyModel);
    if (!model) return false;
    model->SetLocalRotation(0.0f, DirectX::XM_PI, 0.0f);

    m_animation = AddComponent<AnimationComponent>(modelManager, GameAssets::EnemyModel);
    return m_animation != nullptr;
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
