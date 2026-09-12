#include "GregoryShaderHeader.hlsli"

PSOutput GregoryPS(Output input)
{
    PSOutput o;

    float b = saturate(dot(-lightVec, normalize(input.normal)));
    float3 color = float3(b * 0.8 + 0.2, b * 0.8 + 0.2, b * 0.9 + 0.2);

    o.color = float4(color * lerp(0.5f, 1.0f, ShadowFactor(input.tpos)), 1);
    o.normal = float4((normalize(input.normal) + 1.0f) * 0.5f, 1);
    return o;
}