#pragma once

#include "GameObject.h"

class Tree final : public GameObject
{
public:
    bool Initialize(ModelManager& modelManager) override;
    void FaceCamera(const DirectX::XMFLOAT3& cameraPosition);
    DirectX::XMFLOAT4X4 CreateShadowWorldMatrix() const override;
    uint32_t GetRenderFlags() const override { return 8u; }
    void SetVisualScale(float scale)
    {
        m_scale = { scale * 1.05f, scale, scale };
    }
};
