//----------------------------------------------------------------------------
// ShadowDepth.hlsl
// ライト視点から深度を描く。
//
// アルファテストのために、深度専用パスにもピクセルシェーダが必要になった。
// clip() されたピクセルは深度も書き込まれないので、
// 葉の抜けた部分は影を落とさなくなる
//----------------------------------------------------------------------------
#include "Common.hlsli"

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

PSInput VSMain(float3 position : POSITION, float2 uv : TEXCOORD0,
    uint4 joints : BLENDINDICES, float4 weights : BLENDWEIGHT)
{
    PSInput output;
    position = ApplyGrassDeformation(position, weights.x);
    if (jointCount > 0)
    {
        float4x4 skin =
            weights.x * jointMatrices[joints.x] +
            weights.y * jointMatrices[joints.y] +
            weights.z * jointMatrices[joints.z] +
            weights.w * jointMatrices[joints.w];
        position = mul(float4(position, 1.0), skin).xyz;
    }
    output.position = mul(mul(float4(position, 1.0), world), lightViewProj);
    output.uv = uv;
    return output;
}

void PSMain(PSInput input)
{
    // 色は書かないので戻り値は void。
    // 不透明マテリアルでは alphaCutoff が負なので clip は常に通る
    float alpha = g_baseColorMap.Sample(g_modelSampler, input.uv).a;
    clip(alpha - alphaCutoff);
}
