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

PSInput VSMain(float3 position : POSITION, float2 uv : TEXCOORD0)
{
    PSInput output;
    // ワールド行列は単位行列なので、ライトのビュー射影を掛けるだけ
    output.position = mul(float4(position, 1.0), lightViewProj);
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