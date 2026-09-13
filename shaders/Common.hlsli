//----------------------------------------------------------------------------
// Common.hlsli
// 複数のシェーダで共有する定義。
// 定数バッファの定義を一箇所にまとめることで、
// シェーダごとにレイアウトがずれる事故を防ぐ
//----------------------------------------------------------------------------
#ifndef COMMON_HLSLI
#define COMMON_HLSLI

static const float PI = 3.14159265359;

cbuffer SceneConstants : register(b0)
{
    float4x4 mvp;
    float4x4 world;
    float4x4 invViewProj; // スクリーン座標から視線方向を復元するのに使う

    float3 lightDirection;
    float pad0;
    float3 lightColor;
    float pad1;
    float3 ambientColor;
    float pad2;

    float3 cameraPosition;
    float pad3;

    float3 albedo;
    float metallic;
    float roughness;
    float pad4;
    float pad5;
    float pad6;
    float3 emissiveFactor;
    float pad7;
    float4x4 lightViewProj;
    float alphaCutoff;
    float materialOpacity;
    float pad9;
    float pad10;
    uint jointCount;
    float3 pad11;
    float4x4 jointMatrices[80];
};

// 環境マップ(等距円筒図法の HDR)とサンプラ
Texture2D g_envMap                  : register(t0); // HDR 環境マップ
Texture2D g_irradianceMap           : register(t1); // 半球積分済みの放射照度
Texture2D g_prefilterMap            : register(t2); // roughness 別にぼかした環境マップ
Texture2D g_brdfLut                 : register(t3); // BRDF 積分の事前計算テーブル
Texture2D g_baseColorMap            : register(t4); // モデルのアルベド(sRGB 格納)
Texture2D g_metallicRoughnessMap    : register(t5); // G = roughness, B = metallic
Texture2D g_normalMap               : register(t6); // 接空間ノーマルマップ
Texture2D g_occlusionMap            : register(t7); // R = アンビエントオクルージョン
Texture2D g_emissiveMap             : register(t8); // 自己発光(sRGB 格納)
Texture2D g_shadowMap               : register(t9); // ライト視点の深度
SamplerState g_envSampler   : register(s0);     // 環境マップ用
// 環境マップ用(s0)は経度方向が WRAP だが、LUT の横軸は N·V なので
// 端で折り返されると困る。両方向 CLAMP の専用サンプラを用意する
SamplerState g_clampSampler : register(s1);     // LUT 用のサンプラ。
SamplerState g_modelSampler : register(s2);     // モデル用(両方向 WRAP)
// 比較サンプラ。通常のサンプラと違い「値を返す」のではなく
// 「指定した値と比べた結果(0 か 1)」を返し、しかも 2x2 で補間してくれる
SamplerComparisonState g_shadowSampler : register(s3);

//----------------------------------------------------------------------------
// 方向ベクトル → 等距円筒図法(equirectangular)の UV
//   u : 水平方向の角度を 0〜1 に。地球儀の経度にあたる
//   v : 垂直方向の角度を 0〜1 に。緯度にあたる(0 が真上)
//----------------------------------------------------------------------------
float2 DirectionToEquirectUV(float3 dir)
{
    float2 uv;
    uv.x = atan2(dir.z, dir.x) / (2.0 * PI) + 0.5;
    uv.y = acos(clamp(dir.y, -1.0, 1.0)) / PI;
    return uv;
}

//----------------------------------------------------------------------------
// 等距円筒図法の UV → 方向ベクトル
// DirectionToEquirectUV の逆変換。畳み込みで「このテクセルが表す方向」を得る
//----------------------------------------------------------------------------
float3 EquirectUVToDirection(float2 uv)
{
    float phi = (uv.x - 0.5) * 2.0 * PI;
    float theta = uv.y * PI;
    return float3(sin(theta) * cos(phi),
                  cos(theta),
                  sin(theta) * sin(phi));
}

//----------------------------------------------------------------------------
// roughness を考慮したフレネル(環境光用)
//
// 通常の Schlick は「1 本の光線」に対する式なので、
// あらゆる方向から光が来る環境光にそのまま使うと縁が白飛びする。
// 粗い表面ほどフレネルの effect を弱める補正を入れたもの
//----------------------------------------------------------------------------
float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float rough)
{
    float3 inv = float3(1.0 - rough, 1.0 - rough, 1.0 - rough);
    return F0 + (max(inv, F0) - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

//----------------------------------------------------------------------------
// 環境 BRDF の解析的近似(Karis, "Physically Based Shading on Mobile")
//
// split-sum の後半の項。本来はテクスチャに焼いて引くものだが、
// この多項式近似でも実用上ほぼ見分けが付かない。
// 戻り値 x は F0 に掛ける係数、y は加算するバイアス
//----------------------------------------------------------------------------
float2 EnvBRDFApprox(float NoV, float rough)
{
    const float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    const float4 c1 = float4(1.0, 0.0425, 1.040, -0.040);
    float4 r = rough * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    return float2(-1.04, 1.04) * a004 + r.zw;
}

//----------------------------------------------------------------------------
// Hammersley 点列(低食い違い量列)
//
// ランダムより「均等にばらける」サンプル点の作り方。
// ビット列を左右反転させることで、少ないサンプル数でも
// 偏りなく空間を埋められる。モンテカルロ積分の定番手法
//----------------------------------------------------------------------------
float RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

float2 Hammersley(uint i, uint count)
{
    return float2(float(i) / float(count), RadicalInverse_VdC(i));
}

//----------------------------------------------------------------------------
// GGX 重要度サンプリング
//
// 半球を一様に舐めるのではなく、「GGX 分布が大きい方向を優先して」
// サンプルを配る。同じサンプル数でも圧倒的に少ないノイズで済む
//----------------------------------------------------------------------------
float3 ImportanceSampleGGX(float2 Xi, float3 N, float rough)
{
    float a = rough * rough;

    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    // 接空間でのハーフベクトル
    float3 H = float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);

    // 接空間 → ワールド空間
    float3 up = abs(N.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 tangent = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);
    return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

#endif // COMMON_HLSLI
