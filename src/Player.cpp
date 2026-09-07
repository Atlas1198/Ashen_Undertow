#include "Player.h"
#include "CapsuleCollider.h"

#include <cmath>
#include "Input.h"
#include "ModelComponent.h"
#include "GameAssets.h"

bool Player::Initialize(ModelManager& modelManager)
{
    if (!AddComponent<ModelComponent>(modelManager, GameAssets::CharacterModel)) return false;
    return AddComponent<CapsuleCollider>(0.5f, 0.5f) != nullptr;
}

void Player::Update(float deltaTime)
{
    const float moveX = m_input.MoveX();
    const float moveZ = m_input.MoveZ();

    const float length = std::sqrt(moveX * moveX + moveZ * moveZ);
    if (length > 0.0f)
    {
        constexpr float moveSpeed = 3.0f;
        const float localX = moveX / length;
        const float localZ = moveZ / length;
        const float sinYaw = std::sin(m_rotation.y);
        const float cosYaw = std::cos(m_rotation.y);
        const float worldX = localX * cosYaw + localZ * sinYaw;
        const float worldZ = -localX * sinYaw + localZ * cosYaw;
        m_position.x += worldX * moveSpeed * deltaTime;
        m_position.z += worldZ * moveSpeed * deltaTime;
    }
}

ModelHandle Player::GetModelHandle() const
{
    const auto* model = GetComponent<ModelComponent>();
    return model ? model->GetModelHandle() : ModelHandle {};
}
