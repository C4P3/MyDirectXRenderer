Texture2D<float4> tex : register(t0);   // 通常テクスチャ
SamplerState smp : register(s0);         // サンプラー

struct Output
{
    float4 svpos : SV_Position;
    float2 uv : TEXCOORD;
};

cbuffer PostEffect : register(b0)
{
    float4 bkweights[2]; // 16 個の float
};