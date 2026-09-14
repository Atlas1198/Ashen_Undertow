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
    m_effects.push_back(effect);
    return effect.id;
}

std::uint64_t EffectManager::Spawn(
    EffectType type, const DirectX::XMFLOAT3& position)
{
    if (type == EffectType::HitSpark)
    {
        EffectDesc flash;
        flash.type = type;
        flash.position = position;
        flash.startColor = { 5.0f, 2.8f, 0.35f, 1.0f };
        flash.endColor = { 1.0f, 0.1f, 0.01f, 0.0f };
        flash.startSize = 0.42f;
        flash.endSize = 0.05f;
        flash.lifetime = 0.16f;
        const auto firstId = Spawn(flash);

        constexpr int particleCount = 48;
        for (int i = 0; i < particleCount; ++i)
        {
            float x = NextRandom() * 2.0f - 1.0f;
            float y = NextRandom() * 1.6f - 0.45f;
            float z = NextRandom() * 2.0f - 1.0f;
            const float length = std::sqrt(x * x + y * y + z * z);
            if (length <= 0.001f) x = 1.0f;
            else
            {
                x /= length;
                y /= length;
                z /= length;
            }
            const float speed = 3.5f + NextRandom() * 8.0f;
            EffectDesc spark;
            spark.type = type;
            spark.position = position;
            spark.position.x += x * 0.06f;
            spark.position.y += y * 0.06f;
            spark.position.z += z * 0.06f;
            spark.velocity = { x * speed, y * speed, z * speed };
            spark.acceleration = { 0.0f, -4.0f, 0.0f };
            spark.startColor = { 4.5f, 2.0f + NextRandom(), 0.18f, 1.0f };
            spark.endColor = { 1.0f, 0.08f, 0.01f, 0.0f };
            spark.startSize = 0.025f + NextRandom() * 0.035f;
            spark.endSize = 0.008f;
            spark.lifetime = 0.22f + NextRandom() * 0.28f;
            spark.drag = 1.8f;
            Spawn(spark);
        }
        return firstId;
    }
    if (type == EffectType::BlockSpark)
    {
        EffectDesc flash;
        flash.type = type;
        flash.position = position;
        flash.startColor = { 1.8f, 4.0f, 6.0f, 1.0f };
        flash.endColor = { 0.1f, 0.5f, 1.5f, 0.0f };
        flash.startSize = 0.5f;
        flash.endSize = 0.06f;
        flash.lifetime = 0.2f;
        const auto firstId = Spawn(flash);

        constexpr int particleCount = 36;
        for (int i = 0; i < particleCount; ++i)
        {
            float x = NextRandom() * 2.0f - 1.0f;
            float y = NextRandom() * 1.5f - 0.25f;
            float z = NextRandom() * 2.0f - 1.0f;
            const float directionLength = std::sqrt(x * x + y * y + z * z);
            if (directionLength <= 0.001f) x = 1.0f;
            else
            {
                x /= directionLength;
                y /= directionLength;
                z /= directionLength;
            }
            const float speed = 4.5f + NextRandom() * 7.5f;
            EffectDesc spark;
            spark.type = type;
            spark.position = position;
            spark.velocity = { x * speed, y * speed, z * speed };
            spark.acceleration = { 0.0f, -3.0f, 0.0f };
            spark.startColor = {
                0.8f + NextRandom(), 2.8f, 5.5f, 1.0f };
            spark.endColor = { 0.05f, 0.25f, 1.0f, 0.0f };
            spark.startSize = 0.025f + NextRandom() * 0.03f;
            spark.endSize = 0.006f;
            spark.lifetime = 0.18f + NextRandom() * 0.22f;
            spark.drag = 2.4f;
            Spawn(spark);
        }
        return firstId;
    }

    EffectDesc desc;
    desc.type = type;
    desc.position = position;
    switch (type)
    {
    case EffectType::ChargeAura:
        desc.startColor = { 0.35f, 0.7f, 1.0f, 0.8f };
        desc.endColor = { 0.8f, 0.95f, 1.0f, 0.0f };
        desc.startSize = 0.3f;
        desc.endSize = 1.2f;
        desc.lifetime = 0.45f;
        break;
    case EffectType::FootstepDust:
        desc.startColor = { 0.5f, 0.4f, 0.25f, 0.45f };
        desc.endColor = { 0.5f, 0.4f, 0.25f, 0.0f };
        desc.velocity = { 0.0f, 0.35f, 0.0f };
        desc.startSize = 0.18f;
        desc.endSize = 0.5f;
        desc.lifetime = 0.4f;
        break;
    case EffectType::WaterSplash:
        desc.startColor = { 0.55f, 0.85f, 1.0f, 0.65f };
        desc.velocity = { 0.0f, 0.8f, 0.0f };
        desc.acceleration = { 0.0f, -2.0f, 0.0f };
        desc.startSize = 0.2f;
        desc.endSize = 0.6f;
        desc.lifetime = 0.55f;
        break;
    case EffectType::EnemyDefeat:
        desc.startColor = { 0.15f, 0.85f, 1.0f, 0.9f };
        desc.startSize = 0.5f;
        desc.endSize = 1.8f;
        desc.lifetime = 0.8f;
        break;
    case EffectType::BossTelegraph:
        desc.startColor = { 1.0f, 0.12f, 0.05f, 0.65f };
        desc.startSize = 0.4f;
        desc.endSize = 2.5f;
        desc.lifetime = 1.0f;
        break;
    default:
        break;
    }
    return Spawn(desc);
}

void EffectManager::SpawnChargeAura(
    const DirectX::XMFLOAT3& position, float chargeSeconds)
{
    const float charge = std::clamp(chargeSeconds, 0.0f, 3.0f);
    DirectX::XMFLOAT4 startColor;
    DirectX::XMFLOAT4 endColor;
    if (charge < 1.0f)
    {
        startColor = { 3.8f, 3.8f, 3.8f, 0.8f };
        endColor = { 0.8f, 0.8f, 0.8f, 0.0f };
    }
    else if (charge < 2.0f)
    {
        startColor = { 0.25f, 1.2f, 5.0f, 0.85f };
        endColor = { 0.05f, 0.3f, 1.2f, 0.0f };
    }
    else if (charge < 3.0f)
    {
        startColor = { 5.0f, 2.8f, 0.2f, 0.9f };
        endColor = { 1.2f, 0.35f, 0.02f, 0.0f };
    }
    else
    {
        startColor = { 6.0f, 0.25f, 1.0f, 1.0f };
        endColor = { 1.5f, 0.03f, 0.35f, 0.0f };
    }

    const int count = charge >= 3.0f ? 11 :
        (charge >= 2.0f ? 9 : (charge >= 1.0f ? 7 : 5));
    for (int i = 0; i < count; ++i)
    {
        const float angle = NextRandom() * DirectX::XM_2PI;
        const float radius = 0.35f + NextRandom() * 0.32f;
        const float radialX = std::cos(angle);
        const float radialZ = std::sin(angle);
        EffectDesc particle;
        particle.type = EffectType::ChargeAura;
        particle.position = {
            position.x + radialX * radius,
            position.y + 0.03f + NextRandom() * 0.16f,
            position.z + radialZ * radius
        };
        const float swirlSpeed = 0.18f + charge * 0.06f;
        const float inwardSpeed = 0.22f + NextRandom() * 0.18f;
        particle.velocity = {
            -radialX * inwardSpeed - radialZ * swirlSpeed,
            1.35f + NextRandom() * 1.0f + charge * 0.18f,
            -radialZ * inwardSpeed + radialX * swirlSpeed
        };
        particle.acceleration = {
            -radialX * 0.25f, 0.18f, -radialZ * 0.25f
        };
        particle.startColor = startColor;
        particle.endColor = endColor;
        particle.startSize = 0.025f + charge * 0.006f + NextRandom() * 0.025f;
        particle.endSize = 0.006f;
        particle.lifetime = 0.62f + NextRandom() * 0.3f;
        particle.drag = 0.3f;
        Spawn(particle);
    }
}

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

void EffectManager::Stop(std::uint64_t id)
{
    std::erase_if(m_effects,
        [id](const EffectInstance& effect) { return effect.id == id; });
}

void EffectManager::Update(float deltaTime)
{
    if (deltaTime <= 0.0f) return;

    for (auto& effect : m_effects)
    {
        effect.velocity.x += effect.acceleration.x * deltaTime;
        effect.velocity.y += effect.acceleration.y * deltaTime;
        effect.velocity.z += effect.acceleration.z * deltaTime;
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

void EffectManager::Draw(Renderer& renderer) const
{
    if (m_textureIndex == UINT32_MAX) return;
    for (const auto& effect : m_effects)
    {
        const float speed = std::sqrt(
            effect.velocity.x * effect.velocity.x +
            effect.velocity.y * effect.velocity.y +
            effect.velocity.z * effect.velocity.z);
        const bool roundParticle = effect.type == EffectType::ChargeAura ||
            effect.type == EffectType::FootstepDust;
        const float stretch = roundParticle
            ? effect.size : effect.size + speed * 0.055f;
        const DirectX::XMFLOAT3 drawDirection = roundParticle
            ? DirectX::XMFLOAT3 {} : effect.velocity;
        renderer.DrawEffectParticle(m_textureIndex, effect.position,
            drawDirection, effect.color, effect.size, stretch,
            effect.type != EffectType::FootstepDust);
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
