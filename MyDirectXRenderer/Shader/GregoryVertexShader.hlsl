#include "GregoryShaderHeader.hlsli"

Output GregoryVS(float4 pos : POSITION, float4 normal : NORMAL)
{
    Output output;

    pos = mul(world, pos);
    output.svpos = mul(mul(proj, view), pos);

    normal.w = 0; // 平行移動を無効化
    output.normal = mul(world, normal).xyz;

    // GregoryShadowVS が焼いたのと同じ変換
    output.tpos = mul(lightCamera, pos);

    return output;
}
