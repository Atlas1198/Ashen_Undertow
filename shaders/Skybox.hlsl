//-------------------------------------------------
//
//          Skybox.hlsl
//
//  頂点バッファを使わず、画面全体を覆う三角形 1 枚で背景を描く。
//-------------------------------------------------

// 各ピクセルが「どの方向を見ているか」を逆射影で求め、環境マップを引く

#include "Common.hlsli"

struct PSInput
{
    float4 position : SV_POSITION;
    float2 ndc : TEXCOORD0; // 正規化デバイス座標(-1〜+1)
};

//----------------------------------------------------------------------------
// 頂点シェーダ: 頂点 ID だけから座標を作る
//
// 画面より大きい三角形 1 枚で全画面を覆う定番手法。
// 四角形(三角形 2 枚)より速く、対角線上での補間の乱れも起きない
//----------------------------------------------------------------------------
PSInput VSMain(uint id : SV_VertexID)
{
    // id = 0,1,2 から (0,0) (2,0) (0,2) を作る
    float2 uv = float2((id << 1) & 2, id & 2);
    // UV(左上原点) → NDC(中央原点、Y は上向き)へ変換
    float2 ndc = uv * float2(2.0, -2.0) + float2(-1.0, 1.0);

    PSInput output;
    output.position = float4(ndc, 0.0, 1.0);
    output.ndc = ndc;
    return output;
}

//----------------------------------------------------------------------------
// ピクセルシェーダ
//----------------------------------------------------------------------------
float4 PSMain(PSInput input) : SV_TARGET
{
    // このピクセルを、near 面と far 面の 2 点に逆射影する。
    // その 2 点を結ぶ向きが「このピクセルが見ている方向」になる。
    // w で割るのは同次座標から通常の座標に戻すため
    float4 nearH = mul(float4(input.ndc, 0.0, 1.0), invViewProj);
    float4 farH = mul(float4(input.ndc, 1.0, 1.0), invViewProj);
    float3 dir = normalize(farH.xyz / farH.w - nearH.xyz / nearH.w);

    // 環境マップは HDR なので 1.0 を大きく超える値が返る。
    // SampleLevel でミップ 0 を明示的に指定している
    // (全画面三角形では画面内の微分が正しく取れず、Sample だと乱れるため)
    float3 color = g_envMap.SampleLevel(g_envSampler, DirectionToEquirectUV(dir), 0).rgb;
    return float4(color, 1.0);
}