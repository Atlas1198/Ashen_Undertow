//----------------------------------------------------------------------------
// Triangle.hlsl
// Cook-Torrance BRDF による物理ベースレンダリング。
// C++ 側の InputLayout の "POSITION"/"NORMAL" とセマンティクスが対応する。
//----------------------------------------------------------------------------
#include "Common.hlsli"

struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float4 tangent  : TANGENT;
    float2 uv       : TEXCOORD0;
    uint4 joints    : BLENDINDICES;
    float4 weights  : BLENDWEIGHT;
};

struct PSInput
{
    float4 position : SV_POSITION;  // クリップ空間座標
    float3 normal   : NORMAL;       // ワールド空間の法線
    float3 worldPos : TEXCOORD0;    // ワールド空間の位置(視線ベクトル計算用)
    float2 uv       : TEXCOORD1;    // UV座標(テクスチャ用)
    float4 tangent  : TEXCOORD2;    // xyz = 接線、w = 従法線の向き
};

//----------------------------------------------------------------------------
// 頂点シェーダ
//----------------------------------------------------------------------------
PSInput VSMain(VSInput input)
{
    PSInput output;
    float3 localPosition = input.position;
    float3 localNormal = input.normal;
    float3 localTangent = input.tangent.xyz;
    localPosition = ApplyGrassDeformation(localPosition, input.weights.x);
    ApplyWaterWaves(localPosition, localNormal, localTangent);
    if (jointCount > 0)
    {
        float4x4 skin =
            input.weights.x * jointMatrices[input.joints.x] +
            input.weights.y * jointMatrices[input.joints.y] +
            input.weights.z * jointMatrices[input.joints.z] +
            input.weights.w * jointMatrices[input.joints.w];
        localPosition = mul(float4(localPosition, 1.0), skin).xyz;
        localNormal = mul(float4(localNormal, 0.0), skin).xyz;
        localTangent = mul(float4(localTangent, 0.0), skin).xyz;
    }
    output.position = mul(float4(localPosition, 1.0), mvp);

    // 法線は w = 0 で掛けて平行移動の影響を除く
    output.normal = mul(float4(localNormal, 0.0), world).xyz;
    // 位置は w = 1。ピクセルごとの視線ベクトルを作るのに使う
    output.worldPos = mul(float4(localPosition, 1.0), world).xyz;
    
    // UV はそのまま
    output.uv = input.uv;
    
    // 接線もワールド空間へ。w(従法線の向き)はそのまま運ぶ
    output.tangent = float4(mul(float4(localTangent, 0.0), world).xyz,
                            input.tangent.w);

    return output;
}

//----------------------------------------------------------------------------
// D 項: GGX / Trowbridge-Reitz 法線分布関数
//
// 微小鏡のうち、ちょうどハーフベクトル H を向いているものの割合。
// ハイライトの「形」を決める本体で、GGX は裾が長く尾を引くのが特徴。
// 現実の素材の観測結果によく合うため、現在の業界標準になっている
//----------------------------------------------------------------------------
float DistributionGGX(float3 N, float3 H, float rough)
{
    // roughness を 2 乗して使うのは、スライダーの動きを知覚的に
    // 均等にするための業界慣習(Disney の提案)
    float a = rough * rough;
    float a2 = a * a;

    float NdotH = saturate(dot(N, H));
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

//----------------------------------------------------------------------------
// G 項: Smith の幾何減衰(Schlick-GGX 近似)
//
// 微小な凹凸が互いを隠し合って失われる光の割合。
// 面を浅い角度から見たときに不自然に光り過ぎるのを防ぐ
//----------------------------------------------------------------------------
float GeometrySchlickGGX(float NdotX, float rough)
{
    // 直接光用の k。IBL では k = a*a/2 と式が変わる点に注意
    float r = rough + 1.0;
    float k = (r * r) / 8.0;
    return NdotX / (NdotX * (1.0 - k) + k);
}

float GeometrySmith(float3 N, float3 V, float3 L, float rough)
{
    // 「光が入るとき」と「目に届くとき」の 2 回遮蔽されるので 2 つ掛ける
    float NdotV = saturate(dot(N, V));
    float NdotL = saturate(dot(N, L));
    return GeometrySchlickGGX(NdotV, rough) * GeometrySchlickGGX(NdotL, rough);
}

//----------------------------------------------------------------------------
// F 項: フレネル(Schlick 近似)
//
// 見る角度が浅くなるほど反射率が上がる現象。
// 水面を真上から見ると底が見え、遠くを見ると空が映るのと同じ理屈。
// F0 は正面から見たときの反射率
//----------------------------------------------------------------------------
float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

//----------------------------------------------------------------------------
// シャドウマップによる遮蔽の判定。1.0 = 光が当たる、0.0 = 完全な影
//----------------------------------------------------------------------------
float ComputeShadow(float3 worldPos)
{
    // ワールド座標をライト視点のクリップ空間へ
    float4 lightSpace = mul(float4(worldPos, 1.0), lightViewProj);
    // 直交投影なので w は 1 だが、透視投影に変えても動くよう割っておく
    float3 ndc = lightSpace.xyz / lightSpace.w;

    // NDC(-1〜1、Y は上向き)→ テクスチャ UV(0〜1、Y は下向き)
    float2 uv = ndc.xy * float2(0.5, -0.5) + 0.5;

    // シャドウマップの範囲外や、ライトの far より奥は「影ではない」
    if (ndc.z > 1.0)
        return 1.0;

    // PCF(Percentage Closer Filtering):
    // 1 点だけ比較すると、シャドウマップの解像度がそのまま
    // ギザギザの輪郭として出てしまう。周囲も調べて平均する
    const float texelSize = 1.0 / 2048.0;

    float shadow = 0.0;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            // SampleCmpLevelZero は比較サンプラ専用。
            // 「ndc.z 以下か」の判定結果を 2x2 で補間して返すので、
            // 9 回のループでも実質 36 サンプルぶんの滑らかさになる
            shadow += g_shadowMap.SampleCmpLevelZero(
                          g_shadowSampler,
                          uv + float2(x, y) * texelSize,
                          ndc.z);
        }
    }
    return shadow / 9.0;
}

//----------------------------------------------------------------------------
// ピクセルシェーダ
//----------------------------------------------------------------------------
float4 PSMain(PSInput input) : SV_TARGET
{
    //------------------------------------------------------------------
    // ノーマルマッピング
    //------------------------------------------------------------------
    float3 N = normalize(input.normal);

    // 接線を法線に対して直交化する(補間でわずかにずれるため)
    float3 T = normalize(input.tangent.xyz - N * dot(N, input.tangent.xyz));
    // 従法線。w はモデルによって向きが反転しているので掛ける
    float3 B = cross(N, T) * input.tangent.w;

    // テクスチャの 0〜1 を -1〜+1 に展開
    float3 tangentNormal = g_normalMap.Sample(g_modelSampler, input.uv).xyz * 2.0 - 1.0;
    // TBN 行列で接空間 → ワールド空間。
    // mul(v, float3x3(T,B,N)) は v.x*T + v.y*B + v.z*N と同じ
    N = normalize(mul(tangentNormal, float3x3(T, B, N)));

    //------------------------------------------------------------------
    // マテリアルをテクスチャから取り出す
    //------------------------------------------------------------------
    float4 baseColorSample = g_baseColorMap.Sample(g_modelSampler, input.uv);

    if ((renderFlags & RenderFlagTerrain) != 0u)
    {
        const float boundaryNoise =
            sin(input.worldPos.x * 0.31) * sin(input.worldPos.z * 0.27) * 0.16;
        const float height = input.worldPos.y + boundaryNoise;
        const float abovePondBed = smoothstep(-1.85, -1.18, height);
        const float belowRockyHills = 1.0 - smoothstep(0.55, 1.65, height);
        const float grassWeight = saturate(abovePondBed * belowRockyHills);
        const float4 grassSample = g_emissiveMap.Sample(g_modelSampler, input.uv);
        baseColorSample = lerp(baseColorSample, grassSample, grassWeight);
    }

    // アルファテスト。しきい値未満のピクセルはここで捨てられ、
    // 色も深度も書き込まれない。
    // 不透明マテリアルは alphaCutoff が負なので常に通る
    clip(baseColorSample.a - alphaCutoff);

    float3 albedoTex = baseColorSample.rgb * albedo;

    // glTF の規約: G = roughness, B = metallic
    float2 mr = g_metallicRoughnessMap.Sample(g_modelSampler, input.uv).gb;
    float rough = saturate(mr.x * roughness);
    float metal = saturate(mr.y * metallic);

    float ao = g_occlusionMap.Sample(g_modelSampler, input.uv).r;
    float3 emissive = g_emissiveMap.Sample(g_modelSampler, input.uv).rgb * emissiveFactor;

    //------------------------------------------------------------------
    // 直接光(Cook-Torrance)
    //------------------------------------------------------------------
    float3 V = normalize(cameraPosition - input.worldPos);
    float3 L = normalize(-lightDirection);
    float3 H = normalize(V + L);

    float NdotL = saturate(dot(N, L));
    float NdotV = saturate(dot(N, V));

    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedoTex, metal);

    float D = DistributionGGX(N, H, rough);
    float G = GeometrySmith(N, V, L, rough);
    float3 F = FresnelSchlick(saturate(dot(H, V)), F0);

    float3 specular = (D * G * F) / (4.0 * NdotV * NdotL + 0.0001);

    float3 kS = F;
    float3 kD = (1.0 - kS) * (1.0 - metal);
    float3 diffuse = kD * albedoTex / PI;

    // 影は「直接光が届くかどうか」の話なので、環境光には掛けない。
    // 影の中でも空からの光は回り込んでくる
    float shadow = ComputeShadow(input.worldPos);
    float3 Lo = (diffuse + specular) * lightColor * NdotL * shadow;

    //------------------------------------------------------------------
    // 拡散 IBL
    //------------------------------------------------------------------
    float3 irradiance = g_irradianceMap.SampleLevel(
                            g_envSampler, DirectionToEquirectUV(N), 0).rgb;

    float3 kS_ibl = FresnelSchlickRoughness(NdotV, F0, rough);
    float3 kD_ibl = (1.0 - kS_ibl) * (1.0 - metal);
    float3 diffuseIBL = kD_ibl * irradiance * albedoTex;

    //------------------------------------------------------------------
    // 鏡面 IBL(split-sum)
    //------------------------------------------------------------------
    float3 R = reflect(-V, N);

    const float kMaxReflectionLod = 5.0;
    float3 prefiltered = g_prefilterMap.SampleLevel(
                             g_envSampler, DirectionToEquirectUV(R),
                             rough * kMaxReflectionLod).rgb;

    float2 envBRDF = g_brdfLut.SampleLevel(
                             g_clampSampler, float2(NdotV, rough), 0).rg;
    float3 specularIBL = prefiltered * (kS_ibl * envBRDF.x + envBRDF.y);

    // AO は「環境光がどれだけ遮られているか」なので、
    // 直接光ではなく環境光にだけ掛ける
    float3 ambient = (diffuseIBL + specularIBL) * ao;

    //------------------------------------------------------------------
        // トーンマップとガンマ補正はポストプロセス(Tonemap.hlsl)に移した。
    // ここではリニア空間の HDR 値をそのまま出力する
    float3 color = ambient + Lo + emissive;
    return float4(color, materialOpacity);
}
