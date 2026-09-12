#include "SceneShaderHeader.hlsli"
#include "ShadowShaderHeader.hlsli"
#include "BloomShaderHeader.hlsli"

// 地面は原点に固定した水平な板なので、ワールド行列も法線も持たない
struct Output
{
    float4 svpos : SV_POSITION;
    float3 wpos : POSITION; // ワールド座標。グリッドの描画に使う
    float4 tpos : TPOS;     // ライトから見たクリップ空間の座標
};

struct PSOutput
{
    float4 color : SV_Target0;
    float4 normal : SV_Target1;
    float4 bright : SV_Target2; // 高輝度部分（ブルームの元）
};

