#include "BasicShaderHeader.hlsli"

float4 ShadowVS(
    float4 pos : POSITION,
    float4 normal : NORMAL,
    float2 uv : TEXCOORD,
    min16uint2 boneno : BONE_NO,
    min16uint weight : WEIGHT
) : SV_POSITION
{
    float fWeight = float(weight) / 100.0f;
    
    matrix conBone = bones[boneno.x]
        * fWeight
        + bones[boneno.y]
        * (1.0f - fWeight);
    
    pos = mul(world, mul(conBone, pos));

    return mul(lightCamera, pos);
}