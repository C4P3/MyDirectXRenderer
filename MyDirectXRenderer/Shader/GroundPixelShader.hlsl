#include "GroundShaderHeader.hlsli"

PSOutput GroundPS(Output input)
{
    PSOutput o;
    
    // 板は水平なので法線は固定
    const float3 normal = float3(0, 1, 0);
    
    float diffuseB = saturate(dot(-lightVec, normal));

    // 5 単位ごとのグリッド。
    float2 grid = abs(frac(input.wpos.xz / 5.0f) - 0.5f);
    float gridLine = step(min(grid.x, grid.y), 0.02f);
    float3 base = lerp(float3(0.70, 0.70, 0.72), float3(0.55, 0.55, 0.60), gridLine);

    float3 color = base * (diffuseB * 0.6f + 0.4f) * lerp(0.5f, 1.0f, ShadowFactor(input.tpos));
    
    o.color = float4(color, 1);
    o.normal = float4((normal + 1.0f) * 0.5f, 1); // (0,1,0) → (0.5,1,0.5)
    return o;
}
