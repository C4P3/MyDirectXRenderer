#include "peraHeader.hlsli"
#define SAMPLE_AT(ox, oy) tex.Sample(smp, input.uv + float2((ox) * dx, (oy) * dy))

// ディザ用
// 4x4バイエル行列からドットパターンの閾値（0.0 ～ 1.0）を取得
float GetBayerMatrix(float2 pixelPosition)
{
    static const float bayer4x4[16] =
    {
        // バイエル行列の値（0-15）を正規化
         0.0 / 16.0,  8.0 / 16.0,  2.0 / 16.0, 10.0 / 16.0,
        12.0 / 16.0,  4.0 / 16.0, 14.0 / 16.0,  6.0 / 16.0,
         3.0 / 16.0, 11.0 / 16.0,  1.0 / 16.0,  9.0 / 16.0,
        15.0 / 16.0,  7.0 / 16.0, 13.0 / 16.0,  5.0 / 16.0
    };

    // ピクセル座標を 0 ～ 3　のインデックスに変換 
    int x = (int) fmod(abs(pixelPosition.x), 4.0f);
    int y = (int) fmod(abs(pixelPosition.y), 4.0f);

    return bayer4x4[y * 4 + x];
}

float4 ps(Output input) : SV_Target
{
    float4 col = tex.Sample(smp, input.uv);
    // そのまま
    // return col;
    
    // モノクロ
    // float Y = dot(col.rgb, float3(0.299, 0.587, 0.144));
    // return float4(Y, Y, Y, 1);
    
    // 色反転
    // col.rgb = float3(1.0f, 1.0f, 1.0f) - col.rgb;
    // return col;
    // return float4(1.0f - col.rgb, col.a);
    

    // 色の諧調を落とす
    // return float4(col.rgb - fmod(col.rgb, 0.25f), col.a);
    
    // レトロ感（ディザリングをしながら色の諧調を落とす）
    // 1. スクリーン上のピクセル位置からディザパターンを取得（0.0 ～ 1.0）
    // float ditherPattern = GetBayerMatrix(input.svpos.xy);
    // 2. 減色幅2xに合わせてノイズ(+-x)をオフセットして可算
    // float3 colorWithDither = col.rgb + (ditherPattern - 0.5f) * 0.25f;
    // 3. 負の値にならないように安全のためクランプ
    // colorWithDither = saturate(colorWithDither);
    // 4. ポスタライズ
    // float3 finalColor = colorWithDither - fmod(colorWithDither, 0.25f);
    // return float4(finalColor, col.a);
    
    
    // ぼかし処理
    float w, h, levels;
    tex.GetDimensions(0, w, h, levels);
    float dx = 1.0f / w;
    float dy = 1.0f / h;
    float4 ret = float4(0, 0, 0, 0);
    
    // 画素平均化によるぼかし処理
    // ret += tex.Sample(smp, input.uv + float2(-2 * dx, -2 * dy)); // 左上
    // ret += tex.Sample(smp, input.uv + float2(      0, -2 * dy)); // 上
    // ret += tex.Sample(smp, input.uv + float2( 2 * dx, -2 * dy)); // 右上 
    // ret += tex.Sample(smp, input.uv + float2(-2 * dx, 0)); // 左
    // ret += tex.Sample(smp, input.uv + float2(      0, 0)); // 自分
    // ret += tex.Sample(smp, input.uv + float2( 2 * dx, 0)); // 右
    // ret += tex.Sample(smp, input.uv + float2(-2 * dx, 2 * dy)); // 左下
    // ret += tex.Sample(smp, input.uv + float2(      0, 2 * dy)); // 下
    // ret += tex.Sample(smp, input.uv + float2( 2 * dx, 2 * dy)); // 右下 
    // return ret / 9.0f;
    
    // エンボス加工
    // ret += tex.Sample(smp, input.uv + float2(-2 * dx, -2 * dy)) * 2;    // 左上 * 2
    // ret += tex.Sample(smp, input.uv + float2(-2 * dx, 0));              // 左
    // ret += tex.Sample(smp, input.uv + float2(      0, -2 * dy));        // 上
    // ret += tex.Sample(smp, input.uv + float2(      0, 0));              // 自分
    // ret += tex.Sample(smp, input.uv + float2( 2 * dx, 0)) * -1;         // 右 * -1
    // ret += tex.Sample(smp, input.uv + float2(      0, 2 * dy)) * -1;    // 下 * -1
    // ret += tex.Sample(smp, input.uv + float2( 2 * dx, 2 * dy)) * -2;    // 右下 * -2
    // return ret;
    
    // シャープネス
    // ret += tex.Sample(smp, input.uv + float2(0,0)) * 5;// 自分 * 5
    // ret += tex.Sample(smp, input.uv + float2(      0, -2 * dy)) * -1;// 上 * -1
    // ret += tex.Sample(smp, input.uv + float2(-2 * dx,       0)) * -1;// 左 * -1
    // ret += tex.Sample(smp, input.uv + float2( 2 * dx,       0)) * -1;// 右 * -1
    // ret += tex.Sample(smp, input.uv + float2(      0,  2 * dy)) * -1;// 下 * -1
    // return ret;
    
    // 輪郭線抽出
    // ret += tex.Sample(smp, input.uv + float2(0,0)) * 4;// 自分 * 4
    // ret += tex.Sample(smp, input.uv + float2(      0, -2 * dy)) * -1;// 上 * -1
    // ret += tex.Sample(smp, input.uv + float2(-2 * dx,       0)) * -1;// 左 * -1
    // ret += tex.Sample(smp, input.uv + float2( 2 * dx,       0)) * -1;// 右 * -1
    // ret += tex.Sample(smp, input.uv + float2(      0,  2 * dy)) * -1;// 下 * -1
    // 反転
    // float Y = dot(ret.rgb, float3(0.299, 0.587, 0.114));
    // Y = pow(1.0f - Y, 10.0f);
    // Y = step(0.2, Y);
    // return float4(Y, Y, Y, col.a);
    
    // ガウシアンぼかし
    // 最上段
    //ret += SAMPLE_AT(-2,  2) *  1 / 256;
    //ret += SAMPLE_AT(-1,  2) *  4 / 256;
    //ret += SAMPLE_AT( 0,  2) *  6 / 256;
    //ret += SAMPLE_AT( 1,  2) *  4 / 256;
    //ret += SAMPLE_AT( 2,  2) *  1 / 256;
    // 最下段
    //ret += SAMPLE_AT(-2, -2) *  1 / 256;
    //ret += SAMPLE_AT(-1, -2) *  4 / 256;
    //ret += SAMPLE_AT( 0, -2) *  6 / 256;
    //ret += SAMPLE_AT( 1, -2) *  4 / 256;
    //ret += SAMPLE_AT( 2, -2) *  1 / 256;
    // １つ上段
    //ret += SAMPLE_AT(-2,  1) *  4 / 256;
    //ret += SAMPLE_AT(-1,  1) * 16 / 256;
    //ret += SAMPLE_AT( 0,  1) * 24 / 256;
    //ret += SAMPLE_AT( 1,  1) * 16 / 256;
    //ret += SAMPLE_AT( 2,  1) *  4 / 256;
    // １つ下段
    //ret += SAMPLE_AT(-2, -1) *  4 / 256;
    //ret += SAMPLE_AT(-1, -1) * 16 / 256;
    //ret += SAMPLE_AT( 0, -1) * 24 / 256;
    //ret += SAMPLE_AT( 1, -1) * 16 / 256;
    //ret += SAMPLE_AT( 2, -1) *  4 / 256;
    // 中段
    //ret += SAMPLE_AT(-2,  0) *  6 / 256;
    //ret += SAMPLE_AT(-1,  0) * 24 / 256;
    //ret += SAMPLE_AT( 0,  0) * 36 / 256;
    //ret += SAMPLE_AT( 1,  0) * 24 / 256;
    //ret += SAMPLE_AT( 2,  0) *  6 / 256;
    //return ret;
    
    // ガウシアンぼかし
    ret += bkweights[0].x * col; // 中心は 1 回だけ
    for (int i = 1; i < 8; ++i)
    {
        float w = bkweights[i >> 2][i % 4];
        ret += w * tex.Sample(smp, input.uv + float2(i * dx, 0));
        ret += w * tex.Sample(smp, input.uv + float2(-i * dx, 0));
    }
    return float4(ret.rgb, col.a);

}