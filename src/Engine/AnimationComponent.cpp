#include "AnimationComponent.h"

#include <algorithm>
#include <cmath>

#include "GltfLoader.h"
#include "ModelManager.h"

using namespace DirectX;

bool AnimationComponent::Initialize()
{
    m_modelHandle = m_models.Load(m_path);
    m_model = m_models.GetModel(m_modelHandle);
    if (!m_model || m_model->animations.empty() || m_model->skins.empty() ||
        m_model->skins[0].joints.size() > GltfMaxSkinJoints)
    {
        return false;
    }

    const size_t nodeCount = m_model->nodes.size();
    m_translations.resize(nodeCount);
    m_rotations.resize(nodeCount);
    m_scales.resize(nodeCount);
    m_globals.resize(nodeCount);
    m_globalState.resize(nodeCount);
    m_skinMatrices.resize(m_model->skins[0].joints.size());
    for (size_t i = 0; i < m_model->animations.size(); ++i)
    {
        if (m_model->animations[i].name == "Idle")
        {
            m_animationIndex = i;
            break;
        }
    }
    m_time = m_model->animations[m_animationIndex].startTime;
    Update(0.0f);
    return true;
}

void AnimationComponent::Update(float deltaTime)
{
    const auto& animation = m_model->animations[m_animationIndex];
    const float duration = animation.endTime - animation.startTime;
    m_time += deltaTime * m_playbackSpeed;
    if (duration > 0.0f)
    {
        if (m_loop)
            m_time = animation.startTime + std::fmod(m_time - animation.startTime, duration);
        else if (m_time >= animation.endTime)
        {
            m_time = animation.endTime;
            m_finished = true;
        }
    }
    else if (!m_loop)
        m_finished = true;

    for (size_t i = 0; i < m_model->nodes.size(); ++i)
    {
        m_translations[i] = m_model->nodes[i].translation;
        m_rotations[i] = m_model->nodes[i].rotation;
        m_scales[i] = m_model->nodes[i].scale;
    }

    const auto& skin = m_model->skins[0];
    const int rootJoint = skin.joints.empty() ? -1 : skin.joints.front();

    for (const auto& channel : animation.channels)
    {
        if (channel.node < 0 || static_cast<size_t>(channel.node) >= m_model->nodes.size() ||
            channel.times.empty())
        {
            continue;
        }

        const auto upper = std::upper_bound(channel.times.begin(), channel.times.end(), m_time);
        const size_t next = upper == channel.times.end()
            ? channel.times.size() - 1 : static_cast<size_t>(upper - channel.times.begin());
        const size_t previous = next == 0 ? 0 : next - 1;
        float amount = 0.0f;
        if (next != previous)
        {
            const float keyDuration = channel.times[next] - channel.times[previous];
            if (keyDuration > 0.0f)
                amount = (m_time - channel.times[previous]) / keyDuration;
        }

        const auto& a = channel.values[previous];
        const auto& b = channel.values[next];
        const size_t node = static_cast<size_t>(channel.node);
        if (channel.path == GltfAnimationChannel::Path::Rotation)
        {
            XMStoreFloat4(&m_rotations[node], XMQuaternionSlerp(
                XMLoadFloat4(&a), XMLoadFloat4(&b), amount));
        }
        else
        {
            XMFLOAT3 value {
                a.x + (b.x - a.x) * amount,
                a.y + (b.y - a.y) * amount,
                a.z + (b.z - a.z) * amount
            };
            if (channel.path == GltfAnimationChannel::Path::Translation)
            {
                // This Mixamo skeleton's forward motion is stored in local Y.
                // Player movement is handled by gameplay, so keep only the local Z bob.
                if (channel.node == rootJoint)
                {
                    value.x = channel.values.front().x;
                    value.y = channel.values.front().y;
                }
                m_translations[node] = value;
            }
            else
                m_scales[node] = value;
        }
    }

    if (m_blendDuration > 0.0f)
    {
        m_blendElapsed += std::max(deltaTime, 0.0f);
        const float amount = std::min(m_blendElapsed / m_blendDuration, 1.0f);
        for (size_t i = 0; i < m_translations.size(); ++i)
        {
            const auto& fromTranslation = m_blendTranslations[i];
            auto& translation = m_translations[i];
            translation = {
                fromTranslation.x + (translation.x - fromTranslation.x) * amount,
                fromTranslation.y + (translation.y - fromTranslation.y) * amount,
                fromTranslation.z + (translation.z - fromTranslation.z) * amount
            };
            XMStoreFloat4(&m_rotations[i], XMQuaternionSlerp(
                XMLoadFloat4(&m_blendRotations[i]), XMLoadFloat4(&m_rotations[i]), amount));
            const auto& fromScale = m_blendScales[i];
            auto& scale = m_scales[i];
            scale = {
                fromScale.x + (scale.x - fromScale.x) * amount,
                fromScale.y + (scale.y - fromScale.y) * amount,
                fromScale.z + (scale.z - fromScale.z) * amount
            };
        }
        if (amount >= 1.0f)
            m_blendDuration = 0.0f;
    }

    std::fill(m_globalState.begin(), m_globalState.end(), uint8_t { 0 });
    const XMMATRIX mirrorZ = XMMatrixScaling(1.0f, 1.0f, -1.0f);
    for (size_t i = 0; i < skin.joints.size(); ++i)
    {
        const int node = skin.joints[i];
        if (node < 0 || static_cast<size_t>(node) >= m_model->nodes.size() ||
            i >= skin.inverseBindMatrices.size())
        {
            XMStoreFloat4x4(&m_skinMatrices[i], XMMatrixIdentity());
            continue;
        }

        const XMMATRIX inverseBind = XMLoadFloat4x4(&skin.inverseBindMatrices[i]);
        const XMMATRIX skinMatrix = mirrorZ * inverseBind *
            ComputeGlobal(static_cast<size_t>(node)) * mirrorZ;
        XMStoreFloat4x4(&m_skinMatrices[i], skinMatrix);
    }
}

bool AnimationComponent::Play(const char* name, bool loop, float playbackSpeed,
    float blendDuration)
{
    for (size_t i = 0; i < m_model->animations.size(); ++i)
    {
        if (m_model->animations[i].name != name) continue;
        if (m_animationIndex != i || m_finished)
        {
            if (blendDuration > 0.0f && !m_translations.empty())
            {
                m_blendTranslations = m_translations;
                m_blendRotations = m_rotations;
                m_blendScales = m_scales;
                m_blendDuration = blendDuration;
                m_blendElapsed = 0.0f;
            }
            else
            {
                m_blendDuration = 0.0f;
            }
            m_animationIndex = i;
            m_time = m_model->animations[i].startTime;
        }
        m_loop = loop;
        m_playbackSpeed = playbackSpeed >= 0.0f ? playbackSpeed : 1.0f;
        m_finished = false;
        return true;
    }
    return false;
}

void AnimationComponent::SetNormalizedTime(float normalizedTime)
{
    if (!m_model || m_animationIndex >= m_model->animations.size()) return;
    const auto& animation = m_model->animations[m_animationIndex];
    const float amount = std::clamp(normalizedTime, 0.0f, 1.0f);
    m_time = animation.startTime + (animation.endTime - animation.startTime) * amount;
    m_finished = !m_loop && amount >= 1.0f;
}

float AnimationComponent::GetNormalizedTime() const
{
    if (!m_model || m_animationIndex >= m_model->animations.size()) return 0.0f;
    const auto& animation = m_model->animations[m_animationIndex];
    const float duration = animation.endTime - animation.startTime;
    return duration > 0.0f
        ? std::clamp((m_time - animation.startTime) / duration, 0.0f, 1.0f)
        : 0.0f;
}

bool AnimationComponent::GetNodeModelTransform(
    const char* name, XMFLOAT4X4& transform) const
{
    if (!m_model || !name) return false;
    for (size_t i = 0; i < m_model->nodes.size(); ++i)
    {
        if (m_model->nodes[i].name != name || i >= m_globals.size()) continue;
        const XMMATRIX mirrorZ = XMMatrixScaling(1.0f, 1.0f, -1.0f);
        XMStoreFloat4x4(&transform,
            mirrorZ * XMLoadFloat4x4(&m_globals[i]) * mirrorZ);
        return true;
    }
    return false;
}

bool AnimationComponent::IsPlaying(const char* name) const
{
    return m_model && m_animationIndex < m_model->animations.size() &&
        m_model->animations[m_animationIndex].name == name;
}

XMMATRIX AnimationComponent::ComputeGlobal(size_t nodeIndex)
{
    if (m_globalState[nodeIndex] == 2)
        return XMLoadFloat4x4(&m_globals[nodeIndex]);
    if (m_globalState[nodeIndex] == 1)
        return XMMatrixIdentity();

    m_globalState[nodeIndex] = 1;
    XMMATRIX result = XMMatrixScalingFromVector(XMLoadFloat3(&m_scales[nodeIndex])) *
        XMMatrixRotationQuaternion(XMLoadFloat4(&m_rotations[nodeIndex])) *
        XMMatrixTranslationFromVector(XMLoadFloat3(&m_translations[nodeIndex]));
    const int parent = m_model->nodes[nodeIndex].parent;
    if (parent >= 0 && static_cast<size_t>(parent) < m_model->nodes.size())
        result *= ComputeGlobal(static_cast<size_t>(parent));

    XMStoreFloat4x4(&m_globals[nodeIndex], result);
    m_globalState[nodeIndex] = 2;
    return result;
}
