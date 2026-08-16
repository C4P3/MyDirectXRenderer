// 頂点シェーダーからピクセルシェーダーへのやり取りに使う構造体
struct Output
{
    float4 svpos : SV_Position; // システム用頂点座標
    float4 pos : POSITIONT; // 頂点座標
    float4 normal : NORMAL0; // 法線ベクトル
    float4 vnormal : NORMAL1;   // ビュー変換後の法線ベクトル
    float2 uv : TEXCOORD; // uv 値
    float3 ray : VECTOR; // ベクトル
};

Texture2D<float4> tex : register(t0); // 0番スロットに設定されたテクスチャ
Texture2D<float4> sph : register(t1); // 1番スロットに設定されたテクスチャ
Texture2D<float4> spa : register(t2); // 2番スロットに設定されたテクスチャ


SamplerState smp : register(s0); // 0番スロットに設定されたサンプラ

// 定数バッファー
cbuffer cbuff0 : register(b0)
{
    matrix world;
    matrix view;
    matrix proj;
    float3 eye; // 視点
};

// 定数バッファー1
// マテリアル用
cbuffer Material : register(b1)
{
    float4 diffuse;
    float4 specular;
    float3 ambient;
}