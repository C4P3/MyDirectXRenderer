// シャドウマップの参照とサンプリング。
// PMD / Gregory / 地面 で共有するのでレジスタを t4 / s2 に揃えてある。
// 各ルートシグネチャもこの番号で合わせること。
Texture2D<float> lightDepthTex : register(t4);
SamplerComparisonState shadowSmp : register(s2);

// ライトから見たクリップ空間の座標 → 影の強さ。1 = 完全に照らされている
float ShadowFactor(float4 tpos)
{
    // 平行投影なので w は 1 だが、透視投影のライトに替えても済むように割っておく
    float3 posFromLightVP = tpos.xyz / tpos.w;
    float2 uv = (posFromLightVP.xy + float2(1, -1)) * float2(0.5, -0.5);

    // バイアスを引かないと自分自身と比較することになり、
    // 面がまだらに陰るシャドウアクネが出る
    float depth = posFromLightVP.z - 0.001f;

    float width, height;
    lightDepthTex.GetDimensions(width, height);
    float2 texel = 1.0f / float2(width, height);

    // GPUのサンプラー（LINEAR）に1回だけお願いする
    // return lightDepthTex.SampleCmpLevelZero(shadowSmp, uv, depth);
    
    // 3x3 の PCF。1 タップだと影の輪郭がシャドウマップの解像度どおりに階段状になる
    float sum = 0;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            // 比較サンプラーは「渡した深度がシャドウマップより手前か」を 0/1 で返す。
            // アドレスモードが BORDER で境界色が白（深度 1.0）なので、
            // シャドウマップの外に落ちた点は影にならない
            sum += lightDepthTex.SampleCmpLevelZero(shadowSmp, uv + float2(x, y) * texel, depth);
        }
    }
    return sum / 9.0f;
}
