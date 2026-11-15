//-------------------------------------------------
//
//          Tree.h
//
//  樹木の表示、カメラ追従姿勢、影用姿勢を定義する。
//-------------------------------------------------

#pragma once

#include "GameObject.h"

// カメラに正面を向ける樹木。影は別の固定姿勢で描く。
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
