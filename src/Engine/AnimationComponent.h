//-------------------------------------------------
//
//          AnimationComponent.h
//
//  glTF アニメーションの補間とスキニング行列を管理する。
//-------------------------------------------------

#pragma once

#include <DirectXMath.h>
#include <cstdint>
#include <string>
#include <vector>

#include "Component.h"
#include "ModelHandle.h"

class ModelManager;
struct GltfModel;

// glTF のアニメーションを補間し、各ノードの姿勢とスキニング行列を更新する。
class AnimationComponent final : public Component
{
public:
    AnimationComponent(ModelManager& models, const char* path)
        : m_models(models), m_path(path) {}

    bool Initialize() override;
    void Update(float deltaTime) override;
    // 名前でクリップを選ぶ。再生速度は倍率、ブレンド時間は秒単位。
    bool Play(const char* name, bool loop = true, float playbackSpeed = 1.0f,
        float blendDuration = 0.0f);
    void SetNormalizedTime(float normalizedTime);
    float GetNormalizedTime() const;
    bool GetNodeModelTransform(const char* name,
        DirectX::XMFLOAT4X4& transform) const;
    bool IsPlaying(const char* name) const;
    bool IsFinished() const { return m_finished; }
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
    // 再生位置とブレンド経過は秒単位。前姿勢を保持して切替時に補間する。
    float m_time = 0;
    float m_playbackSpeed = 1.0f;
    float m_blendDuration = 0.0f;
    float m_blendElapsed = 0.0f;
    bool m_loop = true;
    bool m_finished = false;
    int m_rootVerticalAxis = -1;
    std::vector<DirectX::XMFLOAT3> m_translations;
    std::vector<DirectX::XMFLOAT4> m_rotations;
    std::vector<DirectX::XMFLOAT3> m_scales;
    // 新しいクリップへ遷移する直前の姿勢。
    std::vector<DirectX::XMFLOAT3> m_blendTranslations;
    std::vector<DirectX::XMFLOAT4> m_blendRotations;
    std::vector<DirectX::XMFLOAT3> m_blendScales;
    std::vector<DirectX::XMFLOAT4X4> m_globals;
    // ノードの階層姿勢を計算済みか記録し、親の重複計算を避ける。
    std::vector<uint8_t> m_globalState;
    std::vector<DirectX::XMFLOAT4X4> m_skinMatrices;
};
