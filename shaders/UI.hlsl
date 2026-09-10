Texture2D imageTexture : register(t0);
SamplerState imageSampler : register(s0);
struct VertexOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};
VertexOutput VSMain(float2 position : POSITION, float2 uv : TEXCOORD0, float4 color : COLOR0)
{
    VertexOutput output;
    output.position = float4(position, 0, 1);
    output.uv = uv;
    output.color = color;
    return output;
}
float4 PSMain(VertexOutput input) : SV_TARGET
{
    return imageTexture.Sample(imageSampler, input.uv) * input.color;
}
