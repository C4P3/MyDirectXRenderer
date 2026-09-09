#include "peraHeader.hlsli"

Texture2D<float4> normalTex : register(t1);

float4 DistortionPS(Output input) : SV_TARGET
{
    // 法線マップは 0..1 で格納されているので -1..1 に戻す
    float2 nrm = normalTex.Sample(smp, input.uv).xy;
    nrm = nrm * 2.0f - 1.0f;

    // その分だけ UV をずらして元画像をサンプルする
    float2 uv = input.uv + nrm * 0.1f;
    return tex.Sample(smp, uv);
}