#include "SceneShaderHeader.hlsli"
#include "ShadowShaderHeader.hlsli"

cbuffer Transform : register(b2)
{
    matrix world;
};

struct Output
{
    float4 svpos : SV_POSITION;
    float3 normal : NORMAL;
    float4 tpos : TPOS; // ライトから見たクリップ空間の座標
};
