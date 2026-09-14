#pragma once

#include <DirectXMath.h>
#include <cstdint>
#include <vector>

class Renderer;

enum class EffectType
{
    Custom,
    HitSpark,
    BlockSpark,
    ChargeAura,
    FootstepDust,
    WaterSplash,
    EnemyDefeat,
    BossTelegraph
};

struct EffectDesc
{
    EffectType type = EffectType::Custom;
    DirectX::XMFLOAT3 position {};
    DirectX::XMFLOAT3 velocity {};
    DirectX::XMFLOAT3 acceleration {};
    DirectX::XMFLOAT4 startColor { 1, 1, 1, 1 };
    DirectX::XMFLOAT4 endColor { 1, 1, 1, 0 };
    float startSize = 1.0f;
    float endSize = 0.0f;
    float lifetime = 1.0f;
    float drag = 0.0f;
};

struct EffectInstance
{
    std::uint64_t id = 0;
    EffectType type = EffectType::Custom;
    DirectX::XMFLOAT3 position {};
    DirectX::XMFLOAT3 velocity {};
    DirectX::XMFLOAT3 acceleration {};
    DirectX::XMFLOAT4 color { 1, 1, 1, 1 };
    DirectX::XMFLOAT4 startColor { 1, 1, 1, 1 };
    DirectX::XMFLOAT4 endColor { 1, 1, 1, 0 };
    float size = 1.0f;
    float startSize = 1.0f;
    float endSize = 0.0f;
    float age = 0.0f;
    float lifetime = 1.0f;
    float drag = 0.0f;
};

class EffectManager
{
public:
    std::uint64_t Spawn(const EffectDesc& desc);
    std::uint64_t Spawn(EffectType type, const DirectX::XMFLOAT3& position);
    void Stop(std::uint64_t id);
    void Update(float deltaTime);
    void Draw(Renderer& renderer) const;
    void SetTextureIndex(std::uint32_t index) { m_textureIndex = index; }
    void Clear() { m_effects.clear(); }

    const std::vector<EffectInstance>& GetActiveEffects() const
    {
        return m_effects;
    }

private:
    float NextRandom();
    std::vector<EffectInstance> m_effects;
    std::uint64_t m_nextId = 1;
    std::uint32_t m_textureIndex = UINT32_MAX;
    std::uint32_t m_randomState = 0x9e3779b9u;
};
