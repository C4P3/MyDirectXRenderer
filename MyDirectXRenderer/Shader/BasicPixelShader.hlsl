#include "BasicShaderHeader.hlsli"

PSOutput BasicPS(Output input)
{
    PSOutput o;
    
    
    // 平行光線ベクトル。シャドウマップを焼いた向きと同じものが b0 で来る
    float3 light = lightVec;
    
    // ライトのカラー
    float3 lightColor = float3(1, 1, 1);
    
    // ディフューズ計算
    float diffuseB = saturate(dot(-light, (float3) input.normal));
    float4 toonDif = toon.Sample(smpToon, float2(0, 1.0 - diffuseB));
    
    // 光の反射ベクトル
    float3 refLight = normalize(reflect(light, input.normal.xyz));
    float specularB = pow(saturate(dot(refLight, -input.ray)), specular.a);
    
    // スフィアマップ用
    float2 sphereMapUV = input.vnormal.xy;
    sphereMapUV = (sphereMapUV + float2(1, -1)) * float2(0.5, -0.5);
    
    // テクスチャカラー
    float4 texColor = tex.Sample(smp, input.uv);

    float shadowWeight = lerp(0.5f, 1.0f, ShadowFactor(input.tpos));

    float4 color = max(
        toonDif     // 輝度（トゥーン）
        // diffuseB  // 輝度
        * diffuse   // ディフューズカラー
        * texColor  // テクスチャカラー
        * sph.Sample(smp, sphereMapUV)  // スフィアマップ
        + spa.Sample(smp, sphereMapUV)  // スフィアマップ
        + float4(specularB * specular.rgb, 1)   // スペキュラ
    ,
        float4((float3)texColor * ambient, 1)   // アンビエント
    );

    
    o.color = float4(color.rgb * shadowWeight, color.a);
    o.normal = float4((normalize(input.normal.xyz) + 1.0f) * 0.5f, 1); // [-1,1] → [0,1]
    return o;
}