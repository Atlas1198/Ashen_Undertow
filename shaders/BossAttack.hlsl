cbuffer BossAttackConstants : register(b0)
{
    float4x4 viewProjection;
    float4 cameraForward;
    float4 attackOriginTime;
    float4 attackDirectionProgress;
    float4 terrain;
};

static const float2 QuadCorners[6] = {
    float2(-0.5, -0.5), float2(-0.5, 0.5), float2(0.5, 0.5),
    float2(-0.5, -0.5), float2(0.5, 0.5), float2(0.5, -0.5)
};

float Hash(uint value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return (value & 0x00ffffffu) / 16777216.0;
}

float TerrainHeight(float x, float z);

struct FlameOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    float seed : TEXCOORD1;
};

FlameOutput FlameVS(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    const float seed = Hash(instanceId * 17u + 3u);
    const float seed2 = Hash(instanceId * 29u + 11u);
    const float seed3 = Hash(instanceId * 47u + 19u);
    const float seed4 = Hash(instanceId * 71u + 23u);
    const float age = frac(attackOriginTime.w * (0.58 + seed2 * 0.42) + seed);
    const float3 forward = normalize(attackDirectionProgress.xyz);
    float3 right = normalize(cross(float3(0.0, 1.0, 0.0), forward));
    if (dot(right, right) < 0.01) right = float3(1.0, 0.0, 0.0);
    const float3 up = normalize(cross(forward, right));

    // 各粒子を赤い攻撃面上へ均等に吹き降ろす。池の内側は着地点にしない。
    const float targetAngle = seed2 * 6.2831853;
    const float targetRadius = sqrt(seed3) * 37.5;
    float2 targetXZ = float2(cos(targetAngle), sin(targetAngle)) * targetRadius;
    float2 fromPond = targetXZ - terrain.xy;
    const float pondDistance = length(fromPond);
    if (pondDistance < terrain.z + 0.8)
    {
        const float2 pondDirection = pondDistance > 0.001
            ? fromPond / pondDistance : float2(cos(targetAngle), sin(targetAngle));
        targetXZ = terrain.xy + pondDirection * (terrain.z + 0.8 + seed4 * 3.0);
    }
    const float3 target = float3(targetXZ.x,
        TerrainHeight(targetXZ.x, targetXZ.y) + 0.35 + seed4 * 1.8, targetXZ.y);
    const float3 control = attackOriginTime.xyz + forward * (18.0 + seed4 * 18.0)
        + up * (5.0 + seed3 * 9.0) + right * (seed2 - 0.5) * 12.0;
    const float travel = pow(age, 0.78);
    const float3 first = lerp(attackOriginTime.xyz, control, travel);
    const float3 second = lerp(control, target, travel);
    float3 center = lerp(first, second, travel);
    const float turbulence = sin(age * 24.0 + attackOriginTime.w * 9.0 + seed * 18.0);
    center += right * turbulence * (0.12 + age * 1.25);
    center.y += sin(age * 3.14159265) * (0.4 + seed4 * 1.4);

    const float2 corner = QuadCorners[vertexId];
    float3 billboardRight = normalize(cross(float3(0.0, 1.0, 0.0), cameraForward.xyz));
    float3 billboardUp = normalize(cross(cameraForward.xyz, billboardRight));
    const float size = lerp(0.32, 3.6 + seed2 * 2.5, age);
    const float3 world = center + billboardRight * corner.x * size
        + billboardUp * corner.y * size * 1.35;

    const float startFade = smoothstep(0.0, 0.10, attackDirectionProgress.w);
    const float endFade = 1.0 - smoothstep(0.82, 1.0, attackDirectionProgress.w);
    const float lifeFade = smoothstep(0.0, 0.08, age) * (1.0 - smoothstep(0.72, 1.0, age));
    const float heat = 1.0 - age;
    FlameOutput output;
    output.position = mul(float4(world, 1.0), viewProjection);
    output.uv = corner + 0.5;
    output.color = float4(lerp(float3(0.01, 0.14, 1.0),
        float3(0.42, 0.92, 1.0), heat * heat),
        lifeFade * startFade * endFade * (0.45 + seed3 * 0.45));
    output.seed = seed;
    return output;
}

float4 FlamePS(FlameOutput input) : SV_TARGET
{
    const float2 p = input.uv * 2.0 - 1.0;
    const float radial = dot(p, p);
    const float flicker = sin((p.x + p.y) * 8.0 + input.seed * 31.0
        + attackOriginTime.w * 13.0) * 0.08;
    const float alpha = saturate((1.0 - radial + flicker) * 2.2) * input.color.a;
    const float core = saturate((0.42 - radial) * 3.0);
    return float4(input.color.rgb + core * float3(0.35, 0.7, 1.0), alpha);
}

float TerrainHeight(float x, float z)
{
    const float radius = sqrt(x * x + z * z);
    const float t = saturate((radius - 14.0) / 28.0);
    const float rim = t * t * (3.0 - 2.0 * t);
    const float ripples = 0.15 * sin(x * 0.48) * sin(z * 0.43)
        + 0.09 * sin(x * 0.3 + z * 0.2);
    const float hills = 2.7 + 0.9 * sin(x * 0.18 + z * 0.14)
        + 0.65 * cos(z * 0.24 - x * 0.09)
        + 0.3 * sin(x * 0.65) * cos(z * 0.55);
    const float pondDistance = length(float2(x - terrain.x, z - terrain.y));
    const float pondT = saturate((7.5 - pondDistance) / 2.0);
    const float basin = pondT * pondT * (3.0 - 2.0 * pondT);
    return -1.35 + ripples + rim * hills - 1.8 * basin;
}

struct TelegraphOutput
{
    float4 position : SV_POSITION;
    float2 worldXZ : TEXCOORD0;
};

TelegraphOutput TelegraphVS(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    static const float2 corners[6] = {
        float2(0, 0), float2(0, 1), float2(1, 1),
        float2(0, 0), float2(1, 1), float2(1, 0)
    };
    const uint x = instanceId % 96u;
    const uint z = instanceId / 96u;
    const float2 worldXZ = float2((float)x - 48.0, (float)z - 48.0) + corners[vertexId];
    const float3 world = float3(worldXZ.x,
        TerrainHeight(worldXZ.x, worldXZ.y) + 0.075, worldXZ.y);
    TelegraphOutput output;
    output.position = mul(float4(world, 1.0), viewProjection);
    output.worldXZ = worldXZ;
    return output;
}

float4 TelegraphPS(TelegraphOutput input) : SV_TARGET
{
    if (length(input.worldXZ) > 38.0) discard;
    if (length(input.worldXZ - terrain.xy) <= terrain.z) discard;
    const float pulse = 0.72 + 0.28 * sin(attackOriginTime.w * 8.0);
    const float sweep = 0.65 + 0.35 * sin(length(input.worldXZ) * 1.7
        - attackOriginTime.w * 12.0);
    return float4(1.0, 0.015, 0.005,
        (0.20 + terrain.w * 0.34) * pulse * sweep);
}
