#include "SceneShaderHeader.hlsli"

Texture2D<float> tex : register(t0);
SamplerState smp : register(s0);

float4 LinearDepthVisualizePS(float4 pos : SV_POSITION, float2 uv : TEXCOORD) : SV_TARGET
{
    // 透視投影の深度は非線形なので、ビュー空間の z に戻してから正規化する
    float d = tex.Sample(smp, uv);
    float z = nearZ * farZ / (farZ - d * (farZ - nearZ));
    float v = saturate((z - nearZ) / (farZ - nearZ));
    return float4(v, v, v, 1);
}