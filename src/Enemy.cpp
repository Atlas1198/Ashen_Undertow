#include "Enemy.h"
#include "Collider.h"
#include "AnimationComponent.h"

#include "ModelComponent.h"
#include "GameAssets.h"

bool Enemy::Initialize(ModelManager& modelManager)
{
    InitializeHP(1000);
    if (!AddComponent<ModelComponent>(modelManager, GameAssets::EnemyModel)) return false;
    if (!AddComponent<AnimationComponent>(modelManager, GameAssets::EnemyModel)) return false;
    auto* collider = AddComponent<CapsuleCollider>(0.4, 0.4);
    if (!collider) return false;
    collider->SetOffset(0.0f, 0.8f, 0.0f);
    return collider != nullptr;
}
