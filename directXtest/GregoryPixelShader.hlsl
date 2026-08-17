#include "GregoryShaderHeader.hlsli"

float4 GregoryPS(Output input) : SV_TARGET
{
    float3 light = normalize(float3(1, -1, 1));
    float b = saturate(dot(-light, normalize(input.normal)));
    return float4(b * 0.8 + 0.2, b * 0.8 + 0.2, b * 0.9 + 0.2, 1);
}