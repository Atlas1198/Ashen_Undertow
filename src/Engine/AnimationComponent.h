#pragma once

#include <DirectXMath.h>
#include <cstdint>
#include <string>
#include <vector>

#include "Component.h"
#include "ModelHandle.h"

class ModelManager;
struct GltfModel;

class AnimationComponent final : public Component
{
public:
    AnimationComponent(ModelManager& models, const char* path)
        : m_models(models), m_path(path) {}

    bool Initialize() override;
    void Update(float deltaTime) override;
    bool Play(const char* name);
    const std::vector<DirectX::XMFLOAT4X4>& GetSkinMatrices() const
    {
        return m_skinMatrices;
    }

private:
    DirectX::XMMATRIX ComputeGlobal(size_t nodeIndex);

    ModelManager& m_models;
    std::string m_path;
    ModelHandle m_modelHandle;
    const GltfModel* m_model = nullptr;
    size_t m_animationIndex = 0;
    float m_time = 0;
    std::vector<DirectX::XMFLOAT3> m_translations;
    std::vector<DirectX::XMFLOAT4> m_rotations;
    std::vector<DirectX::XMFLOAT3> m_scales;
    std::vector<DirectX::XMFLOAT4X4> m_globals;
    std::vector<uint8_t> m_globalState;
    std::vector<DirectX::XMFLOAT4X4> m_skinMatrices;
};
