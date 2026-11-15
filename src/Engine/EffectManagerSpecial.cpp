//-------------------------------------------------
//
//          EffectManagerSpecial.cpp
//
//  特殊攻撃の軌跡と着弾演出を構成する。
//-------------------------------------------------

#include "EffectManager.h"

#include <algorithm>
#include <cmath>

void EffectManager::SpawnSpecialSlash(const DirectX::XMFLOAT3& position,
    const DirectX::XMFLOAT3& forward)
{
    float depthX = -forward.x;
    float depthZ = -forward.z;
    const float depthLength = std::sqrt(depthX * depthX + depthZ * depthZ);
    if (depthLength > 0.001f)
    {
        depthX /= depthLength;
        depthZ /= depthLength;
    }
    else
    {
        depthX = 0.0f;
        depthZ = 1.0f;
    }

    // 三日月面は「進行方向×上下」。正面からは奥行きが潰れて縦線に見える。
    const DirectX::XMFLOAT3 depth { depthX, 0.0f, depthZ };
    const DirectX::XMFLOAT3 up { 0.0f, 1.0f, 0.0f };

    const auto add = [](const DirectX::XMFLOAT3& a,
        const DirectX::XMFLOAT3& b)
    {
        return DirectX::XMFLOAT3 { a.x + b.x, a.y + b.y, a.z + b.z };
    };
    const auto scale = [](const DirectX::XMFLOAT3& value, float amount)
    {
        return DirectX::XMFLOAT3 {
            value.x * amount, value.y * amount, value.z * amount
        };
    };
    const auto pointOnCrescent = [&](float angle, float radius)
    {
        // 右側を開き、左側へ大きく膨らむC字型。
        const float across = (0.56f - std::cos(angle)) * radius;
        const float vertical = std::sin(angle) * radius;
        return add(position, add(scale(depth, across), scale(up, vertical)));
    };
    const auto spawnStrip = [&](const DirectX::XMFLOAT3& p0,
        const DirectX::XMFLOAT3& p1, float width,
        const DirectX::XMFLOAT4& startColor,
        const DirectX::XMFLOAT4& endColor, float lifetime)
    {
        const DirectX::XMFLOAT3 direction {
            p1.x - p0.x, p1.y - p0.y, p1.z - p0.z
        };
        const float length = std::sqrt(direction.x * direction.x +
            direction.y * direction.y + direction.z * direction.z);
        if (length <= 0.001f) return;

        EffectDesc strip;
        strip.type = EffectType::SpecialSlash;
        strip.position = {
            (p0.x + p1.x) * 0.5f,
            (p0.y + p1.y) * 0.5f,
            (p0.z + p1.z) * 0.5f
        };
        strip.velocity = scale(forward, 0.75f);
        strip.visualDirection = direction;
        strip.visualLength = length + width * 0.5f;
        strip.startColor = startColor;
        strip.endColor = endColor;
        strip.startSize = width;
        strip.endSize = width * 0.28f;
        strip.lifetime = lifetime;
        Spawn(strip);
    };

    constexpr int outlineSegments = 36;
    constexpr float radius = 2.8f;
    constexpr float arcStart = -1.36f;
    constexpr float arcEnd = 1.36f;
    for (int i = 0; i < outlineSegments; ++i)
    {
        const float t0 = static_cast<float>(i) / outlineSegments;
        const float t1 = static_cast<float>(i + 1) / outlineSegments;
        const float middle = (t0 + t1) * 0.5f;
        const float taper = std::pow(
            (std::max)(0.0f, std::sin(middle * DirectX::XM_PI)), 0.58f);
        const auto p0 = pointOnCrescent(arcStart + (arcEnd - arcStart) * t0, radius);
        const auto p1 = pointOnCrescent(arcStart + (arcEnd - arcStart) * t1, radius);

        spawnStrip(p0, p1, 0.18f + taper * 0.30f,
            { 0.0f, 2.6f, 2.0f, 0.34f * taper },
            { 0.0f, 0.7f, 0.85f, 0.0f }, 0.18f);
        spawnStrip(p0, p1, 0.04f + taper * 0.08f,
            { 2.8f, 6.0f, 5.5f, 0.96f * taper },
            { 0.10f, 1.5f, 1.65f, 0.0f }, 0.12f);
    }

}

void EffectManager::SpawnSpecialImpact(const DirectX::XMFLOAT3& position,
    const DirectX::XMFLOAT3& forward)
{
    constexpr float flashSizes[] = { 13.0f, 7.5f, 3.5f };
    constexpr DirectX::XMFLOAT4 flashColors[] = {
        { 0.15f, 4.5f, 4.0f, 0.42f },
        { 1.5f, 7.5f, 7.0f, 0.72f },
        { 7.0f, 10.0f, 10.0f, 1.0f }
    };
    for (int i = 0; i < 3; ++i)
    {
        EffectDesc flash;
        flash.type = EffectType::ChargeHit;
        flash.position = {
            position.x - forward.x * static_cast<float>(i) * 0.35f,
            position.y - forward.y * static_cast<float>(i) * 0.35f,
            position.z - forward.z * static_cast<float>(i) * 0.35f
        };
        flash.startColor = flashColors[i];
        flash.endColor = { 0.0f, 0.65f, 1.0f, 0.0f };
        flash.startSize = flashSizes[i];
        flash.endSize = flashSizes[i] * 0.12f;
        flash.lifetime = 0.34f + static_cast<float>(i) * 0.07f;
        Spawn(flash);
    }

    constexpr int sparkCount = 320;
    for (int i = 0; i < sparkCount; ++i)
    {
        float x = NextRandom() * 2.0f - 1.0f;
        float y = NextRandom() * 2.0f - 1.0f;
        float z = NextRandom() * 2.0f - 1.0f;
        const float length = std::sqrt(x * x + y * y + z * z);
        if (length <= 0.001f)
        {
            x = 1.0f;
            y = z = 0.0f;
        }
        else
        {
            x /= length;
            y /= length;
            z /= length;
        }

        const float speed = 9.0f + NextRandom() * 18.0f;
        const float streakLength = 2.5f + NextRandom() * 8.5f;
        EffectDesc spark;
        spark.type = EffectType::SpecialSlash;
        spark.position = {
            position.x + x * 0.55f,
            position.y + y * 0.55f,
            position.z + z * 0.55f
        };
        spark.velocity = {
            x * speed - forward.x * 2.0f,
            y * speed - forward.y * 2.0f,
            z * speed - forward.z * 2.0f
        };
        spark.visualDirection = { x, y, z };
        spark.visualLength = streakLength;
        spark.startColor = NextRandom() > 0.28f
            ? DirectX::XMFLOAT4 { 0.15f, 6.5f, 5.5f, 1.0f }
            : DirectX::XMFLOAT4 { 6.5f, 9.0f, 9.0f, 1.0f };
        spark.endColor = { 0.0f, 0.45f, 0.85f, 0.0f };
        spark.startSize = 0.055f + NextRandom() * 0.12f;
        spark.endSize = 0.012f;
        spark.lifetime = 0.48f + NextRandom() * 0.48f;
        spark.drag = 0.75f;
        Spawn(spark);
    }

    constexpr int emberCount = 80;
    for (int i = 0; i < emberCount; ++i)
    {
        const float angle = NextRandom() * DirectX::XM_2PI;
        const float radialSpeed = 3.0f + NextRandom() * 9.0f;
        EffectDesc ember;
        ember.type = EffectType::ChargeHit;
        ember.position = position;
        ember.velocity = {
            std::cos(angle) * radialSpeed,
            4.0f + NextRandom() * 12.0f,
            std::sin(angle) * radialSpeed
        };
        ember.acceleration = { 0.0f, -5.0f, 0.0f };
        ember.startColor = NextRandom() > 0.4f
            ? DirectX::XMFLOAT4 { 0.1f, 5.0f, 4.5f, 0.9f }
            : DirectX::XMFLOAT4 { 5.0f, 8.0f, 8.0f, 1.0f };
        ember.endColor = { 0.0f, 0.3f, 0.7f, 0.0f };
        ember.startSize = 0.18f + NextRandom() * 0.35f;
        ember.endSize = 0.03f;
        ember.lifetime = 0.7f + NextRandom() * 0.8f;
        ember.drag = 0.45f;
        Spawn(ember);
    }
}
