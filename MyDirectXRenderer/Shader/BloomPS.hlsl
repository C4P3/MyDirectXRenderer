#include "peraHeader.hlsli"
#include "BloomShaderHeader.hlsli"

// 縮小バッファ。1/2, 1/4, ... と縮めたものが縦に積んである
Texture2D<float4> shrinkTex : register(t2);

// 縮小バッファの 1 段を 5x5 のガウシアンで取る。
// b1 のウェイト（σ = 3）の先頭 3 つを縦横に掛け合わせて 2 次元のカーネルにし、
// 打ち切った分は合計で割って正規化する。
//
// rectMin / rectMax は今見ている段の矩形。1 枚のテクスチャに段を詰めているので、
// クランプしないと端のタップが隣の段を拾ってしまう。
float4 GetGaussianBlur(float2 uv, float2 texel, float2 rectMin, float2 rectMax)
{
    float4 ret = float4(0, 0, 0, 0);
    float total = 0.0f;

    [unroll]
    for (int y = -2; y <= 2; ++y)
    {
        [unroll]
        for (int x = -2; x <= 2; ++x)
        {
            float wgt = bkweights[0][abs(x)] * bkweights[0][abs(y)];
            float2 suv = clamp(uv + float2(x, y) * texel, rectMin, rectMax);
            ret += wgt * shrinkTex.Sample(smp, suv);
            total += wgt;
        }
    }
    return ret / total;
}

float4 BloomPS(Output input) : SV_TARGET
{
    float4 col = tex.Sample(smp, input.uv);

    float w, h, level;
    shrinkTex.GetDimensions(0, w, h, level);
    float2 texel = float2(1.0f / w, 1.0f / h);

    // 段を順に舐めて足し込む。タップの幅はどの段でも 1 テクセルだが、
    // 小さい段ほど画面上では広く滲むので、合計すると裾の広いブルームになる
    float4 bloom = float4(0, 0, 0, 0);
    float2 uvSize = float2(1.0f, 0.5f);  // 段 0 は横が全幅、縦が半分
    float2 uvOfst = float2(0.0f, 0.0f);

    [unroll]
    for (int i = 0; i < bloomShrinkLevels; ++i)
    {
        // 半テクセルだけ内側に寄せて、隣の段をサンプルしないようにする
        bloom += GetGaussianBlur(input.uv * uvSize + uvOfst, texel,
                                 uvOfst + texel * 0.5f,
                                 uvOfst + uvSize - texel * 0.5f);
        uvOfst.y += uvSize.y;
        uvSize *= 0.5f;
    }

    // 元の色は SRGB の UNORM に焼かれた時点で 1.0 止まりなので、
    // そこへ 1.0 を超えていた分の滲みを足し戻す形になる
    return float4(col.rgb + saturate(bloom.rgb * bloomStrength), col.a);
}
