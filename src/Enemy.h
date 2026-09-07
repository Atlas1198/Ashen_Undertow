#pragma once

#include "GameObject.h"

class Enemy final : public GameObject
{
public:
    bool Initialize(ModelManager& modelManager) override;
};
