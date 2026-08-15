#include "BasicShaderHeader.hlsli"
Output BasicVS( 
    float4 pos : POSITION, 
    float4 normal : NORMAL,
    float2 uv : TEXCOORD,
min16uint2 boneno : BONE_NO,
min16uint weight : WEIGHT
)
{
    Output output; // ピクセルシェーダーに渡す値
    output.svpos = mul(mul(viewproj, world), pos); // シェーダーでは列優先
    normal.w = 0; // 平行移動成分を向こうにする
    output.normal = mul(world, normal); // 法線にもワールド変換を行う
    output.uv = uv;
	return output;
}