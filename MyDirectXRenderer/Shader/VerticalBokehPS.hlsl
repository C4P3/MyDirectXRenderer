#include "peraHeader.hlsli"

float4 VerticalBokehPS(Output input) : SV_TARGET
{
    float w, h, level;
    tex.GetDimensions(0, w, h, level);
    
    float dy = 1.0f / h;
    float4 ret = float4(0, 0, 0, 0);
    float4 col = tex.Sample(smp, input.uv);
    
    ret += bkweights[0].x * col;
    
    for (int i = 1; i < 8; ++i)
    {
        float w = bkweights[i >> 2][i % 4];
        ret += w * tex.Sample(smp, input.uv + float2(0, dy * i));
        ret += w * tex.Sample(smp, input.uv + float2(0, dy * -i));
    }
    return float4(ret.rgb, col.a);
}