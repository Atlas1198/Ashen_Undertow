//-------------------------------------------------
//
//          BrdfLut.hlsl
//
//  split-sum の後半の項を 2D テーブルに焼く。
//-------------------------------------------------

//   横軸 (u) = N·V     : 見る角度
//   縦軸 (v) = roughness
//   R チャンネル = F0 に掛ける係数、G チャンネル = 加算するバイアス
//
// 環境にもマテリアルにも依存しないので、一度作れば永久に使い回せる

#include "Common.hlsli"

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
// IBL 用の幾何減衰項。
// 直接光では k = (rough+1)²/8 だったが、IBL では k = rough²/2 を使う。
// 同じ Smith の式でも、光源が「1 方向」か「全方向」かで最適な k が違う
//----------------------------------------------------------------------------
float GeometrySchlickGGX_IBL(float NdotX, float rough)
{
    float k = (rough * rough) / 2.0;
    return NdotX / (NdotX * (1.0 - k) + k);
}

float GeometrySmithIBL(float NdotV, float NdotL, float rough)
{
    return GeometrySchlickGGX_IBL(NdotV, rough) * GeometrySchlickGGX_IBL(NdotL, rough);
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float NdotV = input.uv.x;
    float rough = input.uv.y;

    // u = 0 だと N·V = 0 でゼロ除算になるので、わずかに逃がす
    NdotV = max(NdotV, 0.001);

    // 法線を Z 軸に固定した接空間で計算する。
    // BRDF の積分は方位角に依存しないので、これで一般性を失わない
    const float3 N = float3(0.0, 0.0, 1.0);
    float3 V = float3(sqrt(1.0 - NdotV * NdotV), 0.0, NdotV);

    float A = 0.0; // F0 に掛かる係数
    float B = 0.0; // 加算されるバイアス

    const uint kSampleCount = 1024u;

    [loop]
    for (uint i = 0u; i < kSampleCount; ++i)
    {
        float2 Xi = Hammersley(i, kSampleCount);
        float3 H = ImportanceSampleGGX(Xi, N, rough);
        float3 L = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = saturate(L.z);
        float NdotH = saturate(H.z);
        float VdotH = saturate(dot(V, H));

        if (NdotL > 0.0)
        {
            float G = GeometrySmithIBL(NdotV, NdotL, rough);
            // 重要度サンプリングの確率密度で割った結果が、この形に整理される
            float G_Vis = (G * VdotH) / (NdotH * NdotV);
            // フレネルの (1-cos)^5 の部分だけを取り出す
            float Fc = pow(1.0 - VdotH, 5.0);

            // F0 に掛かる項と、そうでない項に分解して別々に積む。
            // これが「F0 を積分の外に出せる」仕組みの正体
            A += (1.0 - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }

    return float4(A / float(kSampleCount), B / float(kSampleCount), 0.0, 1.0);
}