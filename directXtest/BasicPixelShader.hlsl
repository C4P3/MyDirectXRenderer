#include "BasicShaderHeader.hlsli"

float4 BasicPS(Output input) : SV_TARGET
{
    // 平行光線ベクトル
    float3 light = normalize(float3(1, -1, 1));
    
    // ライトのカラー
    float3 lightColor = float3(1, 1, 1);
    
    // ディフューズ計算
    float diffuseB = dot(-light, (float3)input.normal);
    
    // 光の反射ベクトル
    float3 refLight = normalize(reflect(light, input.normal.xyz));
    float specularB = pow(saturate(dot(refLight, -input.ray)), specular.a);
    
    // スフィアマップ用
    float2 sphereMapUV = input.vnormal.xy;
    sphereMapUV = (sphereMapUV + float2(1, -1)) * float2(0.5, -0.5);
    
    // テクスチャカラー
    float4 texColor = tex.Sample(smp, input.uv);
    
    return max(
        diffuseB    // 輝度
        * diffuse   // ディフューズカラー
        * texColor  // テクスチャカラー
        * sph.Sample(smp, sphereMapUV)  // スフィアマップ
        + spa.Sample(smp, sphereMapUV)  // スフィアマップ
        + float4(specularB * specular.rgb, 1)   // スペキュラ
    ,
        float4((float3)texColor * ambient, 1)   // アンビエント
    );
}