#include "GregoryShaderHeader.hlsli"

Output GregoryVS(float4 pos : POSITION, float4 normal : NORMAL)
{
    Output output;
    output.svpos = mul(mul(mul(proj, view), world), pos);
    normal.w = 0; // 平行移動を無効化
    output.normal = mul(world, normal).xyz;
    return output;
}