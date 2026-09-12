// 頂点シェーダーからピクセルシェーダーへのやり取りに使う構造体
#include "SceneShaderHeader.hlsli"
#include "ShadowShaderHeader.hlsli"
#include "BloomShaderHeader.hlsli"

struct Output
{
    float4 svpos : SV_Position; // システム用頂点座標
    float4 pos : POSITIONT; // 頂点座標
    float4 normal : NORMAL0; // 法線ベクトル
    float4 vnormal : NORMAL1;   // ビュー変換後の法線ベクトル
    float2 uv : TEXCOORD; // uv 値
    float3 ray : VECTOR; // ベクトル
    float4 tpos : TPOS; // ライトから見たクリップ空間の座標
};

struct PSOutput
{
    float4 color : SV_Target0;
    float4 normal : SV_Target1;
    float4 bright : SV_Target2; // 高輝度部分（ブルームの元）
};

Texture2D<float4> tex : register(t0); // 0番スロットに設定されたテクスチャ
Texture2D<float4> sph : register(t1); // 1番スロットに設定されたテクスチャ
Texture2D<float4> spa : register(t2); // 2番スロットに設定されたテクスチャ
Texture2D<float4> toon : register(t3);// 3番スロットに設定されたテクスチャ
// シャドウマップ（t4）と比較サンプラー（s2）は ShadowShaderHeader.hlsli にある


SamplerState smp : register(s0); // 0番スロットに設定されたサンプラ
SamplerState smpToon : register(s1); // 1番スロットに設定されたサンプラ


cbuffer Material : register(b1)
{
    float4 diffuse;
    float4 specular;
    float3 ambient;
}

cbuffer Transform : register(b2)
{
    matrix world;   // ワールド変換行列
    matrix bones[256];  // ボーン行列
};