#include "GregoryShaderHeader.hlsli"

float4 GregoryPS(Output input) : SV_TARGET
{
    float b = saturate(dot(-lightVec, normalize(input.normal)));
    float3 color = float3(b * 0.8 + 0.2, b * 0.8 + 0.2, b * 0.9 + 0.2);

    return float4(color * lerp(0.5f, 1.0f, ShadowFactor(input.tpos)), 1);
}
