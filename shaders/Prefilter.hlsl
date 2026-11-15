//-------------------------------------------------
//
//          Prefilter.hlsl
//
//  粗さ別に環境画像を畳み込み、鏡面 IBL 用ミップを作る。
//-------------------------------------------------

// ミップ 0 = roughness 0(鏡面)、最終ミップ = roughness 1(完全に粗い)

#include "Common.hlsli"

// ルート定数で渡される。バッファを作らず値そのものがルートに埋まっている
cbuffer PrefilterParams : register(b1)
{
    float g_roughness;
}

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
    float3 N = normalize(EquirectUVToDirection(input.uv));

    // split-sum の要となる割り切り:
    // 本来 roughness 別のぼかしは「視線の向き」にも依存するが、
    // 視線 = 反射方向 = 法線 と仮定して 1 枚に畳み込む。
    // これにより斜めから見たときの伸びた反射は再現できないが、
    // 実用上ほとんど気にならない
    float3 R = N;
    float3 V = N;

    const uint kSampleCount = 512u;

    float3 prefiltered = float3(0, 0, 0);
    float totalWeight = 0.0;

    [loop]
    for (uint i = 0u; i < kSampleCount; ++i)
    {
        float2 Xi = Hammersley(i, kSampleCount);
        float3 H = ImportanceSampleGGX(Xi, N, g_roughness);
        // V を H で鏡面反射させた方向
        float3 L = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = saturate(dot(N, L));
        if (NdotL > 0.0)
        {
            float3 radiance = g_envMap.SampleLevel(
                                  g_envSampler, DirectionToEquirectUV(L), 0).rgb;
            // irradiance と同じくファイアフライ対策
            radiance = min(radiance, 20.0);

            // N·L で重み付けすると結果が安定することが経験的に知られている
            prefiltered += radiance * NdotL;
            totalWeight += NdotL;
        }
    }

    return float4(prefiltered / max(totalWeight, 0.001), 1.0);
}