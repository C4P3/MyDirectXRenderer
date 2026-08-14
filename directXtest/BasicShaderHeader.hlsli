// 頂点シェーダーからピクセルシェーダーへのやり取りに使う構造体
struct Output
{
    float4 svpos : SV_Position; // システム用頂点座標
    float2 uv : TEXCOORD; // uv 値
};

Texture2D<float4> tex : register(t0); // 0番スロットに設定されたテクスチャ
SamplerState smp : register(s0); // 0番スロットに設定されたサンプラ

// 定数バッファー
cbuffer cbuff0 : register(b0)
{
    matrix mat; // 変換行列
};