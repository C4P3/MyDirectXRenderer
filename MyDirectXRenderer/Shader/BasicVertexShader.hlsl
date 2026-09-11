#include "BasicShaderHeader.hlsli"
Output BasicVS( 
    float4 pos : POSITION, 
    float4 normal : NORMAL,
    float2 uv : TEXCOORD,
    min16uint2 boneno : BONE_NO,
    min16uint weight : WEIGHT
)
{
    Output output;
    
    // 1. ボーンのウェイト計算
    float w = weight / 100.0f; // CPU側で0〜100の整数で送っている前提
    matrix bm = bones[boneno[0]] * w + bones[boneno[1]] * (1 - w); // 線形補間
    
    // 2. ローカル空間でのボーン変換
    pos = mul(bm, pos);
    
    normal.w = 0; // 平行移動成分を無効化（ボーン変換より先にやるのが正解）
    normal = mul(bm, normal); // 法線もボーンの動きに追従させる
    
    // 3. ワールド変換
    pos = mul(world, pos);
    output.pos = pos;
    
    // 法線のワールド・ビュー変換（ブレンドで長さが変わるためnormalize）
    output.normal = normalize(mul(world, normal));
    output.vnormal = normalize(mul(view, output.normal));
    
    // 4. プロジェクション変換
    output.svpos = mul(mul(proj, view), pos);
    
    // 5. ライトから見たクリップ空間の座標。
    //    ShadowVS がシャドウマップに焼いたのと同じ変換で、PS 側で比較する
    output.tpos = mul(lightCamera, pos);
    
    output.uv = uv;
    output.ray = normalize(pos.xyz - eye); // 視線ベクトル
    
	return output;
}