#include "SceneShaderHeader.hlsli"
#include "ShadowShaderHeader.hlsli"
#include "BloomShaderHeader.hlsli"

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

struct PSOutput
{
    float4 color : SV_Target0;
    float4 normal : SV_Target1;
    float4 bright : SV_Target2; // 高輝度部分（ブルームの元）
};
