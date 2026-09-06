//----------------------------------------------------------------------------
// BloomBlur.hlsl
// 分離可能ガウシアンぼかしの 1 方向ぶん。
// 横方向と縦方向で 2 回呼ぶことで 2 次元のぼかしになる。
//
// 9x9 のカーネルを 2 次元でそのまま回すと 81 サンプル必要だが、
// 分離すると 9 + 9 = 18 サンプルで済む。
// さらにバイリニア補間で隣り合う 2 テクセルを 1 回のサンプルで
// 取ることで、9 タップぶんを 5 サンプルに減らしている
//----------------------------------------------------------------------------

cbuffer BloomParams : register(b0)
{
    float4 g_params; // xy = ずらす方向と量(テクセルサイズ × 方向)
}

Texture2D g_source : register(t0);
SamplerState g_linearClamp : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

PSInput VSMain(uint id : SV_VertexID)
{
    float2 uv = float2((id << 1) & 2, id & 2);
    float2 ndc = uv * float2(2.0, -2.0) + float2(-1.0, 1.0);

    PSInput output;
    output.position = float4(ndc, 0.0, 1.0);
    output.uv = uv;
    return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    // テクセルのちょうど中間をサンプリングすると、
    // ハードウェアの補間が 2 テクセルの重み付き平均を返してくれる。
    // その性質を利用して、5 サンプルで 9 タップぶんの精度を得る
    const float weights[3] = { 0.2270270270, 0.3162162162, 0.0702702703 };
    const float offsets[3] = { 0.0, 1.3846153846, 3.2307692308 };

    float3 result = g_source.SampleLevel(g_linearClamp, input.uv, 0).rgb * weights[0];

    [unroll]
    for (int i = 1; i < 3; ++i)
    {
        float2 offset = g_params.xy * offsets[i];
        result += g_source.SampleLevel(g_linearClamp, input.uv + offset, 0).rgb * weights[i];
        result += g_source.SampleLevel(g_linearClamp, input.uv - offset, 0).rgb * weights[i];
    }

    return float4(result, 1.0);
}