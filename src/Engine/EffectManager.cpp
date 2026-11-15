//-------------------------------------------------
//
//          EffectManager.cpp
//
//  一時エフェクトの寿命・速度・色・サイズを更新して描画へ渡す。
//-------------------------------------------------

#include "EffectManager.h"
#include "Renderer.h"

#include <algorithm>
#include <cmath>

namespace
{
float Lerp(float start, float end, float amount)
{
    return start + (end - start) * amount;
}
}

std::uint64_t EffectManager::Spawn(const EffectDesc& desc)
{
    if (desc.lifetime <= 0.0f || !std::isfinite(desc.lifetime)) return 0;

    EffectInstance effect;
    effect.id = m_nextId++;
    effect.type = desc.type;
    effect.position = desc.position;
    effect.velocity = desc.velocity;
    effect.acceleration = desc.acceleration;
    effect.startColor = desc.startColor;
    effect.endColor = desc.endColor;
    effect.startSize = (std::max)(0.0f, desc.startSize);
    effect.endSize = (std::max)(0.0f, desc.endSize);
    effect.color = effect.startColor;
    effect.size = effect.startSize;
    effect.lifetime = desc.lifetime;
    effect.drag = (std::max)(0.0f, desc.drag);
    effect.strength = desc.strength;
    effect.visualDirection = desc.visualDirection;
    effect.visualLength = (std::max)(0.0f, desc.visualLength);
    m_effects.push_back(effect);
    return effect.id;
}

void EffectManager::Stop(std::uint64_t id)
{
    std::erase_if(m_effects,
        [id](const EffectInstance& effect) { return effect.id == id; });
}

// 加速度・抵抗で位置を進め、寿命割合から見た目を補間して期限切れを除く。
void EffectManager::Update(float deltaTime)
{
    if (deltaTime <= 0.0f) return;

    for (auto& effect : m_effects)
    {
        effect.velocity.x += effect.acceleration.x * deltaTime;
        effect.velocity.y += effect.acceleration.y * deltaTime;
        effect.velocity.z += effect.acceleration.z * deltaTime;
        // フレーム時間に応じた減衰で速度を落とす。
        const float damping = 1.0f / (1.0f + effect.drag * deltaTime);
        effect.velocity.x *= damping;
        effect.velocity.y *= damping;
        effect.velocity.z *= damping;
        effect.position.x += effect.velocity.x * deltaTime;
        effect.position.y += effect.velocity.y * deltaTime;
        effect.position.z += effect.velocity.z * deltaTime;
        effect.age += deltaTime;

        const float progress = std::clamp(
            effect.age / effect.lifetime, 0.0f, 1.0f);
        effect.size = Lerp(effect.startSize, effect.endSize, progress);
        effect.color = {
            Lerp(effect.startColor.x, effect.endColor.x, progress),
            Lerp(effect.startColor.y, effect.endColor.y, progress),
            Lerp(effect.startColor.z, effect.endColor.z, progress),
            Lerp(effect.startColor.w, effect.endColor.w, progress)
        };
    }

    std::erase_if(m_effects, [](const EffectInstance& effect)
    {
        return effect.age >= effect.lifetime;
    });
}

// 種類に応じて波紋・丸い粒子・速度方向の軌跡へ描画方法を振り分ける。
void EffectManager::Draw(Renderer& renderer) const
{
    if (m_textureIndex == UINT32_MAX) return;
    for (const auto& effect : m_effects)
    {
        if (effect.type == EffectType::WaterRipple)
        {
            renderer.DrawWaterRipple(effect.position, effect.age,
                effect.strength);
            continue;
        }
        const float speed = std::sqrt(
            effect.velocity.x * effect.velocity.x +
            effect.velocity.y * effect.velocity.y +
            effect.velocity.z * effect.velocity.z);
        const bool roundParticle = effect.type == EffectType::ChargeAura ||
            effect.type == EffectType::ChargeHit ||
            effect.type == EffectType::FootstepDust ||
            effect.type == EffectType::EnemyDefeat;
        const bool fixedTrail = effect.visualLength > 0.0f;
        const float trailFade = 1.0f - 0.35f * std::clamp(
            effect.age / effect.lifetime, 0.0f, 1.0f);
        const float stretch = fixedTrail
            ? effect.visualLength * trailFade
            : (roundParticle ? effect.size : effect.size + speed * 0.055f);
        const DirectX::XMFLOAT3 drawDirection = fixedTrail
            ? effect.visualDirection
            : (roundParticle ? DirectX::XMFLOAT3 {} : effect.velocity);
        renderer.DrawEffectParticle(m_textureIndex, effect.position,
            drawDirection, effect.color, effect.size, stretch,
            effect.type != EffectType::FootstepDust &&
            effect.type != EffectType::SwordTrail, fixedTrail);
    }
}

float EffectManager::NextRandom()
{
    m_randomState ^= m_randomState << 13;
    m_randomState ^= m_randomState >> 17;
    m_randomState ^= m_randomState << 5;
    return static_cast<float>(m_randomState & 0x00ffffffu) /
        static_cast<float>(0x01000000u);
}
