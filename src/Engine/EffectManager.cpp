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
    if (type == EffectType::EnemyDefeat)
    {
        std::uint64_t firstId = 0;
        constexpr int particleCount = 220;
        for (int i = 0; i < particleCount; ++i)
        {
            const float height = NextRandom() * 3.05f;
            float centerX = 0.0f;
            float radius = 0.42f;
            if (height < 1.05f)
            {
                centerX = NextRandom() < 0.5f ? -0.30f : 0.30f;
                radius = 0.18f;
            }
            else if (height > 2.35f)
            {
                radius = 0.30f;
            }
            else if (NextRandom() < 0.22f)
            {
                centerX = NextRandom() < 0.5f ? -0.62f : 0.62f;
                radius = 0.20f;
            }

            const float angle = NextRandom() * DirectX::XM_2PI;
            const float spread = std::sqrt(NextRandom()) * radius;
            const float outwardX = std::cos(angle);
            const float outwardZ = std::sin(angle);
            EffectDesc dust;
            dust.type = type;
            dust.position = {
                position.x + centerX + outwardX * spread,
                position.y + 0.10f + height,
                position.z + outwardZ * spread
            };
            const float outwardSpeed = 0.08f + NextRandom() * 0.32f;
            dust.velocity = {
                outwardX * outwardSpeed,
                0.42f + NextRandom() * 0.95f + height * 0.08f,
                outwardZ * outwardSpeed
            };
            dust.acceleration = { 0.0f, 0.10f, 0.0f };
            const float brightness = 0.7f + NextRandom() * 0.7f;
            dust.startColor = {
                0.22f * brightness, 0.78f * brightness,
                1.0f * brightness, 0.82f };
            dust.endColor = { 0.08f, 0.24f, 0.32f, 0.0f };
            dust.startSize = 0.018f + NextRandom() * 0.035f;
            dust.endSize = 0.006f;
            dust.lifetime = 0.85f + NextRandom() * 0.85f;
            dust.drag = 0.35f;
            const auto id = Spawn(dust);
            if (firstId == 0) firstId = id;
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

void EffectManager::SpawnChargeHit(
    const DirectX::XMFLOAT3& position, float chargePower)
{
    chargePower = std::clamp(chargePower, 0.4f, 1.0f);
    DirectX::XMFLOAT4 coreColor;
    DirectX::XMFLOAT4 fadeColor;
    if (chargePower >= 0.95f)
    {
        coreColor = { 6.0f, 0.25f, 2.6f, 1.0f };
        fadeColor = { 1.2f, 0.02f, 0.35f, 0.0f };
    }
    else if (chargePower >= 0.65f)
    {
        coreColor = { 6.0f, 3.6f, 0.35f, 1.0f };
        fadeColor = { 1.2f, 0.25f, 0.01f, 0.0f };
    }
    else
    {
        coreColor = { 1.2f, 4.2f, 6.5f, 1.0f };
        fadeColor = { 0.05f, 0.45f, 1.4f, 0.0f };
    }

    EffectDesc flash;
    flash.type = EffectType::ChargeHit;
    flash.position = position;
    flash.startColor = coreColor;
    flash.endColor = fadeColor;
    flash.startSize = 0.45f + chargePower * 0.75f;
    flash.endSize = 0.10f;
    flash.lifetime = 0.18f + chargePower * 0.12f;
    Spawn(flash);

    const int sparkCount = 45 + static_cast<int>(chargePower * 105.0f);
    for (int i = 0; i < sparkCount; ++i)
    {
        float x = NextRandom() * 2.0f - 1.0f;
        float y = NextRandom() * 1.7f - 0.45f;
        float z = NextRandom() * 2.0f - 1.0f;
        const float length = std::sqrt(x * x + y * y + z * z);
        if (length <= 0.001f) x = 1.0f;
        else
        {
            x /= length;
            y /= length;
            z /= length;
        }
        const float speed = 5.0f + NextRandom() * (8.0f + chargePower * 9.0f);
        EffectDesc spark;
        spark.type = EffectType::HitSpark;
        spark.position = position;
        spark.velocity = { x * speed, y * speed, z * speed };
        spark.acceleration = { 0.0f, -3.0f, 0.0f };
        spark.startColor = coreColor;
        spark.endColor = fadeColor;
        spark.startSize = 0.025f + NextRandom() * (0.035f + chargePower * 0.025f);
        spark.endSize = 0.006f;
        spark.lifetime = 0.25f + NextRandom() * (0.25f + chargePower * 0.25f);
        spark.drag = 1.15f;
        Spawn(spark);
    }

    const int moteCount = 8 + static_cast<int>(chargePower * 18.0f);
    for (int i = 0; i < moteCount; ++i)
    {
        const float angle = NextRandom() * DirectX::XM_2PI;
        EffectDesc mote;
        mote.type = EffectType::ChargeHit;
        mote.position = position;
        mote.velocity = {
            std::cos(angle) * (0.8f + NextRandom() * 1.8f),
            0.4f + NextRandom() * 1.6f,
            std::sin(angle) * (0.8f + NextRandom() * 1.8f)
        };
        mote.startColor = coreColor;
        mote.endColor = fadeColor;
        mote.startSize = 0.05f + NextRandom() * 0.08f;
        mote.endSize = 0.015f;
        mote.lifetime = 0.45f + NextRandom() * 0.45f;
        mote.drag = 1.8f;
        Spawn(mote);
    }
}

void EffectManager::SpawnSwordTrail(
    const std::array<SwordTrailPose, 4>& poses)
{
    const auto pointOnBlade = [](const DirectX::XMFLOAT3& start,
        const DirectX::XMFLOAT3& end, float amount)
    {
        return DirectX::XMFLOAT3 {
            start.x + (end.x - start.x) * amount,
            start.y + (end.y - start.y) * amount,
            start.z + (end.z - start.z) * amount
        };
    };
    const auto catmullRom = [](const DirectX::XMFLOAT3& p0,
        const DirectX::XMFLOAT3& p1, const DirectX::XMFLOAT3& p2,
        const DirectX::XMFLOAT3& p3, float amount)
    {
        const float amount2 = amount * amount;
        const float amount3 = amount2 * amount;
        const auto value = [amount, amount2, amount3](
            float a, float b, float c, float d)
        {
            return 0.5f * ((2.0f * b) + (-a + c) * amount +
                (2.0f * a - 5.0f * b + 4.0f * c - d) * amount2 +
                (-a + 3.0f * b - 3.0f * c + d) * amount3);
        };
        return DirectX::XMFLOAT3 {
            value(p0.x, p1.x, p2.x, p3.x),
            value(p0.y, p1.y, p2.y, p3.y),
            value(p0.z, p1.z, p2.z, p3.z)
        };
    };
    const auto curvePoint = [&](float bladeAmount, float curveAmount)
    {
        const auto p0 = pointOnBlade(poses[0].start, poses[0].end, bladeAmount);
        const auto p1 = pointOnBlade(poses[1].start, poses[1].end, bladeAmount);
        const auto p2 = pointOnBlade(poses[2].start, poses[2].end, bladeAmount);
        const auto p3 = pointOnBlade(poses[3].start, poses[3].end, bladeAmount);
        return catmullRom(p0, p1, p2, p3, curveAmount);
    };
    const auto spawnStroke = [&](float bladeAmount, float curveStart,
        float curveEnd, int layer)
    {
        const auto previous = curvePoint(bladeAmount, curveStart);
        const auto current = curvePoint(bladeAmount, curveEnd);
        const DirectX::XMFLOAT3 movement {
            current.x - previous.x,
            current.y - previous.y,
            current.z - previous.z
        };
        const float length = std::sqrt(movement.x * movement.x +
            movement.y * movement.y + movement.z * movement.z);
        if (length <= 0.012f || length > 2.5f) return;

        EffectDesc trail;
        trail.type = layer > 0 ? EffectType::HitSpark : EffectType::SwordTrail;
        trail.position = {
            (previous.x + current.x) * 0.5f,
            (previous.y + current.y) * 0.5f,
            (previous.z + current.z) * 0.5f
        };
        trail.visualDirection = movement;
        trail.visualLength = length + 0.018f;
        if (layer == 2)
        {
            trail.startColor = { 2.6f, 2.9f, 3.4f, 0.82f };
            trail.endColor = { 0.55f, 0.75f, 1.2f, 0.0f };
            trail.startSize = 0.038f;
            trail.endSize = 0.012f;
            trail.lifetime = 0.13f;
        }
        else if (layer == 1)
        {
            trail.startColor = { 0.65f, 0.90f, 1.8f, 0.34f };
            trail.endColor = { 0.15f, 0.28f, 0.72f, 0.0f };
            trail.startSize = 0.14f;
            trail.endSize = 0.035f;
            trail.lifetime = 0.12f;
        }
        else
        {
            const float edgeWeight = bladeAmount * bladeAmount;
            trail.startColor = {
                0.34f + edgeWeight * 0.55f,
                0.48f + edgeWeight * 0.62f,
                1.05f + edgeWeight * 0.65f,
                0.16f + edgeWeight * 0.22f
            };
            trail.endColor = { 0.18f, 0.24f, 0.55f, 0.0f };
            trail.startSize = 0.105f;
            trail.endSize = 0.025f;
            trail.lifetime = 0.105f;
        }
        Spawn(trail);
    };

    constexpr int ribbonSamples = 22;
    constexpr int curveSegments = 5;
    for (int curve = 0; curve < curveSegments; ++curve)
    {
        const float curveStart = static_cast<float>(curve) / curveSegments;
        const float curveEnd = static_cast<float>(curve + 1) / curveSegments;
        for (int i = 0; i < ribbonSamples; ++i)
        {
            const float bladeAmount = 0.12f + 0.88f *
                static_cast<float>(i) / static_cast<float>(ribbonSamples - 1);
            spawnStroke(bladeAmount, curveStart, curveEnd, 0);
        }
        spawnStroke(0.985f, curveStart, curveEnd, 1);
        spawnStroke(1.0f, curveStart, curveEnd, 2);
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
