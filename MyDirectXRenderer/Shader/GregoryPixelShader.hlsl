#include "GregoryShaderHeader.hlsli"

PSOutput GregoryPS(Output input)
{
    PSOutput o;

    float b = saturate(dot(-lightVec, normalize(input.normal)));
    float3 color = float3(b * 0.8 + 0.2, b * 0.8 + 0.2, b * 0.9 + 0.2);

    // 飽和前の最終カラー。青だけ b = 1 で 1.1 まで伸びるので、そこが高輝度に引っかかる
    float3 ret = color * lerp(0.5f, 1.0f, ShadowFactor(input.tpos));

    o.color = float4(ret, 1);
    o.normal = float4((normalize(input.normal) + 1.0f) * 0.5f, 1);
    o.bright = BrightPass(ret);
    return o;
}