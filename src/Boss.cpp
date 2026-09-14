#include "Boss.h"

#include "AnimationComponent.h"
#include "GameAssets.h"
#include "ModelComponent.h"

#include <algorithm>

namespace
{
constexpr float BossVisualScale = 40.0f;
constexpr float MiniCopySpawnInterval = 8.0f;
}

bool Boss::Initialize(ModelManager& modelManager)
{
    InitializeHP(5000);
    m_scale = { BossVisualScale, BossVisualScale, BossVisualScale };

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
