//-------------------------------------------------
//
//          EffectManagerCharge.cpp
//
//  溜め攻撃のオーラと命中パーティクルを構成する。
//-------------------------------------------------

#include "EffectManager.h"

#include <algorithm>
#include <cmath>

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

