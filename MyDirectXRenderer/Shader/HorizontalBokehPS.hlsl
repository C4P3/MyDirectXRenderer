#include "peraHeader.hlsli"

float4 HorizontalBokehPS(Output input) : SV_TARGET
{
    float w, h, level;
    tex.GetDimensions(0, w, h, level);

    float dx = 1.0f / w;
    float4 ret = float4(0, 0, 0, 0);
    float4 col = tex.Sample(smp, input.uv);

    ret += bkweights[0].x * col;

    for (int i = 1; i < 8; ++i)
    {
        float weight = bkweights[i >> 2][i % 4];
        ret += weight * tex.Sample(smp, input.uv + float2(dx * i, 0));
        ret += weight * tex.Sample(smp, input.uv + float2(dx * -i, 0));
    }
    return float4(ret.rgb, col.a);
}