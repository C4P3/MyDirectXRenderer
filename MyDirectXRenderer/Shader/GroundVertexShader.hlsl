#include "GroundShaderHeader.hlsli"

Output GroundVS(float4 pos : POSITION)
{
    Output output;
    output.svpos = mul(mul(proj, view), pos);
    output.wpos = pos.xyz;
    output.tpos = mul(lightCamera, pos);
    return output;
}
