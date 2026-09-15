#include "EffectManager.h"

#include <algorithm>
#include <cmath>

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
