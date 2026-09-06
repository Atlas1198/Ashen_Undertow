//----------------------------------------------------------------------------
// Irradiance.hlsl
// 環境マップを半球積分して、拡散 IBL 用の放射照度マップを作る。
// 起動時に一度だけ実行される
//----------------------------------------------------------------------------
#include "Common.hlsli"

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

PSInput VSMain(uint id : SV_VertexID)
{
    // 全画面三角形。uv は画面内で 0〜1 になる
    float2 uv = float2((id << 1) & 2, id & 2);
    float2 ndc = uv * float2(2.0, -2.0) + float2(-1.0, 1.0);

    PSInput output;
    output.position = float4(ndc, 0.0, 1.0);
    output.uv = uv;
    return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    // このテクセルが表す法線方向
    float3 N = normalize(EquirectUVToDirection(input.uv));

    // N を軸とする接空間の基底を作る。
    // N が真上に近いときは up と平行になって cross が壊れるので軸を変える
    float3 up = abs(N.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 tangent = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);

    float3 irradiance = float3(0, 0, 0);
    float sampleCount = 0.0;

    // 半球を球面座標で格子状に走査する。
    // 刻みを細かくすると綺麗になるが、起動時間が伸びる
    const float sampleDelta = 0.05;

    [loop]
    for (float phi = 0.0; phi < 2.0 * PI; phi += sampleDelta)
    {
        [loop]
        for (float theta = 0.0; theta < 0.5 * PI; theta += sampleDelta)
        {
            // 接空間での方向(Z が法線方向)
            float3 tangentSample = float3(sin(theta) * cos(phi),
                                          sin(theta) * sin(phi),
                                          cos(theta));

            // 接空間 → ワールド空間
            float3 sampleVec = tangentSample.x * tangent
                             + tangentSample.y * bitangent
                             + tangentSample.z * N;

            // cos(theta) : 斜めから入る光が弱くなる分(ランバートの余弦則)
            // sin(theta) : 球面座標で格子を切ったときの面積の偏りを打ち消す補正
            float3 radiance = g_envMap.SampleLevel(
                                  g_envSampler, DirectionToEquirectUV(sampleVec), 0).rgb;

            // ファイアフライ対策:
            // 太陽のような「極小・超高輝度」の光源は、この程度の
            // サンプル数では正しく積分できず、当たったテクセルだけが
            // 突出して明るくなる。上限を設けて暴れを抑える
            radiance = min(radiance, 20.0);

            irradiance += radiance * cos(theta) * sin(theta);
            sampleCount += 1.0;
        }
    }

    // PI が付くのは、拡散 BRDF の 1/PI と対になる正規化
    irradiance = PI * irradiance / sampleCount;
    return float4(irradiance, 1.0);
}