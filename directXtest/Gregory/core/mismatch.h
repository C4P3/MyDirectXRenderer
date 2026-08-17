#pragma once
#include <vector>

#include "core/patch_mesh.h"

namespace greg {

// パッチ網の「G1 がどれだけ成立しているか」を測る。
//
// 何を測るか: 共有辺を等間隔に刻み、その点での両パッチの法線がなす角（度）。
// 0° なら接平面が一致している = G1 接続が成立している。README の
// 「max 16.2° → 0.03°」はこの値のこと。
//
// なぜ core に置くか: 計算は曲面評価だけで GL に触らない。ここに置くと
//   - ビューア（ヒートマップの色）と回帰テストが同じ 1 つの実装を使う
//   - GL コンテキストなしでテストできる（tests/ はヘッドレスで回る。DESIGN §6）
// 以前はビューア側に埋め込まれており、テストは別の指標（法線外積の大きさ）で
// 独自に測っていた。
//
// なお test_g1.cpp が使う |nA × nB| は「向きの反転に依存しない」指標で、
// パッチの向き付けを問わない厳密性の検証に向く。こちらの acos(dot) は
// 「見た目の折れ角」に直結するので、UI 表示と人間向けの数値報告に使う。

// 共有辺上の 1 サンプル
struct MismatchSample {
    glm::vec3 position;   // A 側の曲面上の点
    glm::vec3 normal;     // A 側の法線（ヒートマップを面から浮かせるのに使う）
    float degrees;        // A 側と B 側の法線がなす角
};

struct MismatchStats {
    float maxDegrees = 0.0f;
    float meanDegrees = 0.0f;
    int sampleCount = 0;
};

// mesh の全共有辺を (segmentsPerEdge + 1) 点で刻み、法線の食い違いを測る。
// samples が非 null なら各サンプルを push_back する（呼び出し側の既存要素は残す）。
// 共有辺が無ければ全て 0 を返す。
MismatchStats measureNormalMismatch(const PatchMesh& mesh, int segmentsPerEdge,
                                    std::vector<MismatchSample>* samples = nullptr);

} // namespace greg
