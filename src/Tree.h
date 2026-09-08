#pragma once

#include "GameObject.h"

class Tree final : public GameObject
{
public:
    bool Initialize(ModelManager& modelManager) override;
    void FaceCamera(const DirectX::XMFLOAT3& cameraPosition);
};
