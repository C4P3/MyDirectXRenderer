#include "GregoryShaderHeader.hlsli"

Output GregoryVS(float4 pos : POSITION, float4 normal : NORMAL)
{
    Output output;
    output.svpos = mul(mul(mul(proj, view), world), pos);
    normal.w = 0; // •½sˆÚ“®‚ğ–³Œø‰»
    output.normal = mul(world, normal).xyz;
    return output;
}