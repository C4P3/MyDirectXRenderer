#include "GregoryShaderHeader.hlsli"

// 影パス用。ライトから見た深度だけを書くのでピクセルシェーダーは無い
float4 GregoryShadowVS(float4 pos : POSITION, float4 normal : NORMAL) : SV_POSITION
{
    return mul(lightCamera, mul(world, pos));
}
