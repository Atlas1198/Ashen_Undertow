#pragma once

#include <DirectXMath.h>
#include <string>
#include "Component.h"
#include "ModelHandle.h"

class ModelManager;

class ModelComponent final : public Component
{
public:
    ModelComponent(ModelManager& models, const std::string& path) : m_models(models), m_path(path) {}
    bool Initialize() override;
    void Draw(Renderer& renderer) override;
    ModelHandle GetModelHandle() const { return m_model; }
    DirectX::XMFLOAT4X4 CreateModelWorldMatrix() const;
    void SetOffset(float x, float y, float z) { m_offset = { x, y, z }; }
    // モデルの正面補正用。Objectの移動方向には影響しない（ラジアン）。
    void SetLocalRotation(float x, float y, float z) { m_localRotation = { x, y, z }; }

private:
    ModelManager& m_models;
    std::string m_path;
    ModelHandle m_model;
    DirectX::XMFLOAT3 m_offset { 0, 0, 0 };
    DirectX::XMFLOAT3 m_localRotation { 0, 0, 0 };
};
