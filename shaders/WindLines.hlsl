//-------------------------------------------------
//
//          WindLines.hlsl
//
//  風の筋をインスタンスごとに生成して描く。
//-------------------------------------------------

#include "Common.hlsli"

// 一本の風筋を複数の短い帯へ分け、緩い曲がりを与える。
static const uint SegmentCount = 8;

struct PSInput
{
    float4 position : SV_POSITION;
    float2 ribbon : TEXCOORD0;
};

uint Hash(uint value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    return value ^ (value >> 16);
}

float Random01(uint seed)
{
    return float(Hash(seed) & 0x00ffffffu) / 16777215.0;
}

// インスタンス ID から決定的な位置を作り、時刻で風向へ流す。
PSInput VSMain(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    static const float2 corners[6] =
    {
        float2(0, -1), float2(0, 1), float2(1, 1),
        float2(0, -1), float2(1, 1), float2(1, -1)
    };

    const uint segment = vertexId / 6;
    const float2 corner = corners[vertexId % 6];
    const float t = (float(segment) + corner.x) / float(SegmentCount);
    const uint seed = instanceId * 9781u + 17u;

    float2 direction = normalize(windDirection.xz);
    const float2 across = float2(-direction.y, direction.x);
    // 風力の強弱から移動速度を分離し、常に一定速度で一方向へ進ませる。
    const float travelDistance = 92.0;
    const float windLineSpeed = 3.0;
    const float travel = (frac(Random01(seed + 2u)
        + windTime * windLineSpeed / travelDistance) - 0.5) * travelDistance;
    const float sideOffset = (Random01(seed + 3u) - 0.5) * 82.0;
    const float length = 2.2 + Random01(seed + 4u) * 3.8;
    const float height = -0.2 + Random01(seed + 5u) * 5.2;
    const float phase = Random01(seed + 6u) * PI * 2.0;

    const float along = (t - 0.5) * length;
    const float lift = sin(t * PI + phase) * (0.05 + 0.10 * Random01(seed + 7u));
    float3 center = float3(direction * travel + across * sideOffset, 0.0).xzy;
    center.y = height + lift;
    center.xz += direction * along;

    const float3 tangent = normalize(float3(direction.x, cos(t * PI + phase) * 0.08,
        direction.y));
    const float3 toCamera = normalize(cameraPosition - center);
    float3 ribbonSide = cross(tangent, toCamera);
    ribbonSide *= rsqrt(max(dot(ribbonSide, ribbonSide), 0.0001));
    const float width = 0.012 + Random01(seed + 8u) * 0.020;

    PSInput output;
    output.position = mul(float4(center + ribbonSide * corner.y * width, 1.0), mvp);
    output.ribbon = float2(t, corner.y);
    return output;
}

// 両端と帯の端を薄くして風筋の切れ目を目立たなくする。
float4 PSMain(PSInput input) : SV_TARGET
{
    const float endFade = smoothstep(0.0, 0.18, input.ribbon.x)
        * (1.0 - smoothstep(0.82, 1.0, input.ribbon.x));
    const float edgeFade = saturate(1.0 - abs(input.ribbon.y));
    const float alpha = 0.16 * windStrength * endFade * edgeFade;
    return float4(0.72, 0.86, 0.90, alpha);
}
