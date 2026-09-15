#include "EffectManager.h"

#include <algorithm>
#include <cmath>

void EffectManager::SpawnFootstepDust(const DirectX::XMFLOAT3& position,
    const DirectX::XMFLOAT3& moveDirection, bool evasion)
{
    const int count = evasion ? 18 : 3;
    for (int i = 0; i < count; ++i)
    {
        const float angle = NextRandom() * DirectX::XM_2PI;
        const float spreadX = std::cos(angle);
        const float spreadZ = std::sin(angle);
        const float radius = NextRandom() * (evasion ? 0.38f : 0.2f);
        const float backwardSpeed = evasion
            ? 0.8f + NextRandom() * 1.5f
            : 0.15f + NextRandom() * 0.35f;
        const float sideSpeed = (NextRandom() * 2.0f - 1.0f) *
            (evasion ? 0.9f : 0.3f);

        EffectDesc dust;
        dust.type = EffectType::FootstepDust;
        dust.position = {
            position.x + spreadX * radius,
            position.y + 0.02f + NextRandom() * 0.06f,
            position.z + spreadZ * radius
        };
        dust.velocity = {
            -moveDirection.x * backwardSpeed + moveDirection.z * sideSpeed,
            (evasion ? 0.45f : 0.22f) + NextRandom() * 0.35f,
            -moveDirection.z * backwardSpeed - moveDirection.x * sideSpeed
        };
        dust.acceleration = { spreadX * 0.15f, 0.08f, spreadZ * 0.15f };
        dust.startColor = evasion
            ? DirectX::XMFLOAT4 { 0.48f, 0.35f, 0.2f, 0.52f }
            : DirectX::XMFLOAT4 { 0.42f, 0.32f, 0.2f, 0.35f };
        dust.endColor = { 0.32f, 0.25f, 0.16f, 0.0f };
        dust.startSize = (evasion ? 0.12f : 0.07f) + NextRandom() * 0.08f;
        dust.endSize = (evasion ? 0.55f : 0.3f) + NextRandom() * 0.18f;
        dust.lifetime = (evasion ? 0.48f : 0.32f) + NextRandom() * 0.25f;
        dust.drag = 2.2f;
        Spawn(dust);
    }
}

void EffectManager::SpawnWaterSplash(const DirectX::XMFLOAT3& position,
    const DirectX::XMFLOAT3& moveDirection, float intensity)
{
    intensity = std::clamp(intensity, 0.25f, 1.6f);
    SpawnWaterRipple(position, intensity);
    const int count = 3 + static_cast<int>(intensity * 9.0f);
    float forwardX = moveDirection.x;
    float forwardZ = moveDirection.z;
    const float forwardLength = std::sqrt(
        forwardX * forwardX + forwardZ * forwardZ);
    if (forwardLength <= 0.001f)
    {
        forwardX = 0.0f;
        forwardZ = 1.0f;
    }
    else
    {
        forwardX /= forwardLength;
        forwardZ /= forwardLength;
    }
    const float sideX = forwardZ;
    const float sideZ = -forwardX;
    for (int i = 0; i < count; ++i)
    {
        const float sideAmount = NextRandom() * 2.0f - 1.0f;
        const float sideSpeed = sideAmount *
            (0.55f + NextRandom() * 1.45f) * intensity;
        const float forwardSpeed =
            (0.1f + NextRandom() * 0.65f) * intensity;
        EffectDesc splash;
        splash.type = EffectType::WaterSplash;
        splash.position = {
            position.x + sideX * sideAmount * NextRandom() * 0.22f,
            position.y + 0.02f,
            position.z + sideZ * sideAmount * NextRandom() * 0.22f
        };
        splash.velocity = {
            sideX * sideSpeed + forwardX * forwardSpeed,
            0.75f + NextRandom() * 2.0f * intensity,
            sideZ * sideSpeed + forwardZ * forwardSpeed
        };
        splash.acceleration = { 0.0f, -5.8f, 0.0f };
        splash.startColor = { 0.65f, 1.55f, 2.4f, 0.68f };
        splash.endColor = { 0.12f, 0.4f, 0.75f, 0.0f };
        splash.startSize = 0.018f + NextRandom() * 0.032f;
        splash.endSize = 0.008f;
        splash.lifetime = 0.28f + NextRandom() * 0.34f;
        splash.drag = 0.55f;
        Spawn(splash);
    }
}

void EffectManager::SpawnWaterRipple(
    const DirectX::XMFLOAT3& position, float intensity)
{
    constexpr int maxRipples = 12;
    int rippleCount = 0;
    for (const auto& effect : m_effects)
        if (effect.type == EffectType::WaterRipple) ++rippleCount;
    if (rippleCount >= maxRipples)
    {
        const auto oldest = std::find_if(m_effects.begin(), m_effects.end(),
            [](const EffectInstance& effect)
            {
                return effect.type == EffectType::WaterRipple;
            });
        if (oldest != m_effects.end()) m_effects.erase(oldest);
    }

    EffectDesc ripple;
    ripple.type = EffectType::WaterRipple;
    ripple.position = position;
    ripple.lifetime = 2.4f;
    ripple.strength = std::clamp(intensity, 0.25f, 1.6f);
    Spawn(ripple);
}

