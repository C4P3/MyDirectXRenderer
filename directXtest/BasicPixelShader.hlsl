#include "BasicShaderHeader.hlsli"

float4 BasicPS(Output input) : SV_TARGET
{
    float3 light = normalize(float3(1, -1, 1));
    float brightness = dot(-light, (float3)input.normal);
    return float4(brightness, brightness, brightness, 1) * diffuse * tex.Sample(smp, input.uv);
}