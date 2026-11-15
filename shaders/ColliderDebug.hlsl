//-------------------------------------------------
//
//          ColliderDebug.hlsl
//
//  デバッグ用コライダー線を画面へ投影する。
//-------------------------------------------------

cbuffer DebugConstants : register(b0)
{
    float4x4 viewProjection;
};

struct VSInput
{
    float3 position : POSITION;
    float4 color : COLOR;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

// 衝突状態に応じた頂点色を変えずに投影する。
PSInput VSMain(VSInput input)
{
    PSInput output;
    output.position = mul(float4(input.position, 1.0f), viewProjection);
    output.color = input.color;
    return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    return input.color;
}
