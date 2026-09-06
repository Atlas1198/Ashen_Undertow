#pragma once

#include "GameObject.h"
#include "ModelManager.h"

class Player final : public GameObject
{
public:
    Player() = default;
    ~Player() override = default;

    bool Initialize(ModelManager& modelManager);
    ModelHandle GetModelHandle() const { return m_modelHandle; }

private:
    ModelHandle m_modelHandle;
};
