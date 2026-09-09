#include "Enemy.h"
#include "SphereCollider.h"
#include "AnimationComponent.h"

#include "ModelComponent.h"
#include "GameAssets.h"

bool Enemy::Initialize(ModelManager& modelManager)
{
    if (!AddComponent<ModelComponent>(modelManager, GameAssets::EnemyModel)) return false;
    if (!AddComponent<AnimationComponent>(modelManager, GameAssets::EnemyModel)) return false;
    return AddComponent<SphereCollider>(0.5f) != nullptr;
}
