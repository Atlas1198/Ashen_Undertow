#include "Enemy.h"
#include "SphereCollider.h"

#include "ModelComponent.h"
#include "GameAssets.h"

bool Enemy::Initialize(ModelManager& modelManager)
{
    if (!AddComponent<ModelComponent>(modelManager, GameAssets::CharacterModel)) return false;
    return AddComponent<SphereCollider>(0.5f) != nullptr;
}
