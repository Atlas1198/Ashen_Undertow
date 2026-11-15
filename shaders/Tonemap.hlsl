//-------------------------------------------------
//
//          Tonemap.hlsl
//
//  HDR バッファを表示可能な 0〜1 に圧縮し、バックバッファへ書き出す。
//-------------------------------------------------

//
// トーンマップとガンマ補正を「絵を描くシェーダ」から切り離し、
// 最後に 1 回だけ通る場所に集約した。
// ブルームや被写界深度も、今後この位置に足していくことになる


cbuffer TonemapParams : register(b0)
{
    float g_exposure;
    float g_bloomIntensity;
}

Texture2D g_hdrBuffer : register(t0);
Texture2D g_bloom : register(t1);
SamplerState g_pointClamp : register(s0);

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

//----------------------------------------------------------------------------
// ACES フィルミックトーンマップ(Narkowicz による近似)
//----------------------------------------------------------------------------
float3 ACESFilm(float3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float3 color = g_hdrBuffer.SampleLevel(g_pointClamp, input.uv, 0).rgb;

    // ブルームを加算する。
    // トーンマップより前、リニア空間で足すのが正しい。
    // 低解像度から拡大されるが、元がぼけているので粗さは見えない
    color += g_bloom.SampleLevel(g_pointClamp, input.uv, 0).rgb * g_bloomIntensity;

    color = ACESFilm(color);
    // リニア空間 → sRGB
    color = pow(color, 1.0 / 2.2);

    return float4(color, 1.0);
}