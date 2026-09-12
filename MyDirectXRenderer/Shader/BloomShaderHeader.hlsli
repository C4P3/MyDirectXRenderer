// 高輝度部分の抽出。PMD / Gregory / 地面 の 3 つのピクセルシェーダから呼ぶので、
// しきい値と抽出式はここ 1 箇所にまとめる（ShadowShaderHeader.hlsli と同じ理由）。
//
// 渡すのは「飽和前の」最終カラー。SV_Target0 は SRGB の UNORM なので、
// 書き込まれた後の値を見ても 1.0 で頭打ちになっていて高輝度かどうか判定できない。
// 書き込み先（SV_Target2）は float16 なので、1.0 を超えた値はそのまま残る。

// 今はまだ発光物が無いので、しきい値だけで拾う
static const float bloomThreshold = 0.5f;

// 縮小バッファに積む段数。Application.cpp の bloom_shrink_levels と合わせること
static const int bloomShrinkLevels = 8;

// 合成時にブルームへ掛ける強さ。眩しすぎるときはここを下げる
static const float bloomStrength = 0.1f;

float4 BrightPass(float3 color)
{
    // しきい値を超えた画素だけを、明るさを保ったまま残す
    float luminance = dot(color, float3(0.299f, 0.587f, 0.114f));
    return float4(color * step(bloomThreshold, luminance), 1);

    // 抽出が効いているかだけを見たいとき用：超えた画素を真っ赤に塗る
    // return float4(any(color > bloomThreshold) ? float3(1, 0, 0) : float3(0, 0, 0), 1);
}
