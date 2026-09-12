#include "peraHeader.hlsli"

float4 NormalVisualizePS(Output input) : SV_TARGET
{
    return float4(tex.Sample(smp, input.uv).rgb, 1);
}