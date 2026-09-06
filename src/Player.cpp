#include "Player.h"

bool Player::Initialize(ModelManager& modelManager)
{
    m_modelHandle = modelManager.Load("assets/model.glb");
    return m_modelHandle.IsValid();
}
