//-------------------------------------------------
//
//          EffectManagerSwordTrail.cpp
//
//  直近の剣位置をつなぎ、残像の粒子を構成する。
//-------------------------------------------------

#include "EffectManager.h"

#include <algorithm>
#include <cmath>

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


