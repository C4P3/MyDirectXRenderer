Texture2D<float> tex : register(t0);
SamplerState smp : register(s0);

float4 DepthVisualizePS(float4 pos : SV_POSITION, float2 uv : TEXCOORD) : SV_TARGET
{
    float d = pow(tex.Sample(smp, uv), 20);
    return float4(d, d, d, 1);
}