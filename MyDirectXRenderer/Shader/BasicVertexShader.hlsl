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
    
    float w = weight / 100.0f;
    matrix bm = bones[boneno[0]] * w + bones[boneno[1]] * (1 - w); // 線形補間
    pos = mul(bm, pos); // 先にボーン変換
    output.svpos = mul(mul(mul(proj, view), world), pos); // シェーダーでは列優先
    output.pos = mul(world, pos);
    normal.w = 0; // 平行移動成分を向こうにする
    output.normal = mul(world, normal); // 法線にもワールド変換を行う
    output.vnormal = mul(view, output.normal);
    output.uv = uv;
    output.ray = normalize(pos.xyz - eye); // 視線ベクトル
	return output;
}