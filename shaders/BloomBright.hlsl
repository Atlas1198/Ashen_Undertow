//-------------------------------------------------
//
//          BloomBright.hlsl
//
//  HDR 画像の明るい部分を抽出する。
//-------------------------------------------------

// 同時に 1/4 解像度へ縮小する。
//
// この処理が成立するのは、HDR バッファが 1.0 を超える値を
// 保持しているからこそ。8bit バッファでは全部 1.0 で頭打ちになり、
// 「どれだけ明るいか」の情報が残らない


cbuffer BloomParams : register(b0)
{
    float4 g_params; // x = 閾値、zw = 入力テクスチャのテクセルサイズ
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
    const float2 texel = g_params.zw;

    // 4 倍縮小なので、1 点だけ拾うと元の 16 ピクセルのうち 1 つしか
    // 見ないことになり、細かい光がフレームごとにチラつく。
    // バイリニアの 4 タップで周囲をならしておく
    float3 color = float3(0, 0, 0);
    color += g_source.SampleLevel(g_linearClamp, input.uv + texel * float2(-1, -1), 0).rgb;
    color += g_source.SampleLevel(g_linearClamp, input.uv + texel * float2(1, -1), 0).rgb;
    color += g_source.SampleLevel(g_linearClamp, input.uv + texel * float2(-1, 1), 0).rgb;
    color += g_source.SampleLevel(g_linearClamp, input.uv + texel * float2(1, 1), 0).rgb;
    color *= 0.25;

    // 太陽のような極端な値をそのまま通すと、ぼかしたとき画面全体が
    // 白く飛ぶ。上限を設けておく
    color = min(color, 20.0);

    // 閾値を超えた「ぶんだけ」を取り出す。
    // 単純に if で切ると境界が硬くなるので、超過量の比率で滑らかに落とす
    float brightness = max(color.r, max(color.g, color.b));
    float contribution = max(brightness - g_params.x, 0.0) / max(brightness, 0.0001);

    return float4(color * contribution, 1.0);
}
