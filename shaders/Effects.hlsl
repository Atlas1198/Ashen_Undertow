cbuffer EffectConstants : register(b0)
{
    float4x4 viewProjection;
    float4 cameraForward;
};

Texture2D particleTexture : register(t0);
SamplerState particleSampler : register(s0);

struct VSInput
{
    float3 position : POSITION;
    float3 direction : DIRECTION;
    float4 color : COLOR;
    float2 size : SIZE;
    uint effectFlags : EFFECTFLAGS;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    nointerpolation uint effectFlags : TEXCOORD1;
};

VSOutput VSMain(VSInput input, uint vertexId : SV_VertexID)
{
    static const float2 corners[6] = {
        float2(-0.5, -0.5), float2(-0.5, 0.5), float2(0.5, 0.5),
        float2(-0.5, -0.5), float2(0.5, 0.5), float2(0.5, -0.5)
    };
    const float2 corner = corners[vertexId];
    const float speed = length(input.direction);
    float3 along = speed > 0.001
        ? input.direction / speed : float3(0.0, 1.0, 0.0);
    float3 side = cross(along, cameraForward.xyz);
    if (dot(side, side) < 0.0001)
        side = cross(float3(0.0, 1.0, 0.0), cameraForward.xyz);
    side = normalize(side);
    if (speed <= 0.001)
        along = normalize(cross(cameraForward.xyz, side));

    const float3 worldPosition = input.position +
        side * corner.x * input.size.x +
        along * corner.y * input.size.y;
    VSOutput output;
    output.position = mul(float4(worldPosition, 1.0), viewProjection);
    output.uv = float2(corner.x + 0.5, 0.5 - corner.y);
    output.color = input.color;
    output.effectFlags = input.effectFlags;
    return output;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    if ((input.effectFlags & 1u) != 0u)
    {
        const float across = 1.0 - smoothstep(
            0.34, 0.50, abs(input.uv.x - 0.5));
        return float4(input.color.rgb, input.color.a * across);
    }
    const float4 particle = particleTexture.Sample(particleSampler, input.uv);
    return float4(particle.rgb * input.color.rgb,
        particle.a * input.color.a);
}
