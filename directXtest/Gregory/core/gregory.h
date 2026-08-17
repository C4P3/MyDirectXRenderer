#pragma once
#include <glm/vec3.hpp>

// 双3次 Gregory パッチの定義と CPU 評価器。このプロジェクトの土台にあたる。
//
// ここで決めた「制御点 20 個の並び順」に他のほぼ全てが依存している:
//   core/patch_mesh.cpp  辺 ↔ 内部制御点の対応表 kEdges
//   core/g1.cpp          補正が書き込む制御点の決定
//   app/overlay_renderer 制御点ネットの線のつなぎ方（kNetLineIndices）
//   gpu/shaders/gregory_eval.glsl   GPU 側の同じ規約（kGridF）
// そのため下の enum のコメントが実質的な仕様書になっている。
// 読む順序は docs/CODE_MAP.md を参照。
namespace greg {

// 内部点ブレンドの分母ガード（DESIGN §3.1「角の 0/0 特異点」）。
// GLSL 実装（gregory_eval.glsl）でも必ず同じ値・同じ処理を使うこと
// —— 差があると tests/test_gpu.cpp の CPU/GPU 一致テストが落ちる。
inline constexpr float kBlendEps = 1e-6f;

// 双3次 Gregory パッチの制御点インデックス規約（Takeda 2008 式(2.7)–(2.9)）。
// 命名 Pijk: i = u 方向 (0..3), j = v 方向 (0..3), k = 内部ペア識別 (0/1)。
// 境界 12 点は k=0 のみ。内部 (i,j ∈ {1,2}) は 2 点 1 組で、式(2.9) により
//   k=0 が u 系の重み（i=1 なら u、i=2 なら 1-u）
//   k=1 が v 系の重み（j=1 なら v、j=2 なら 1-v）
// で混合される。
//
// この規約から出る大事な性質: v 辺（v=0,1）上では k=0 側、u 辺上では k=1 側が
// 生き残る。つまり内部 8 点はそれぞれ「ちょうど 1 つの辺の cross 微分」だけを司る
// ので、共有辺ごとの G1 補正が書き込む先は互いに素になり、適用順序に依存しない
// （NOTES.md §1）。
//
// 4×4 グリッド上の配置（i が横 = u 方向、j が縦 = v 方向）:
//
//   v=1  P030 ── P130 ── P230 ── P330
//         │       :        :       │
//        P020   P120/1   P220/1   P320     内部 4 セルはそれぞれ 2 点 1 組
//         │       :        :       │       （P120/1 = P120 と P121 の意）
//        P010   P110/1   P210/1   P310
//         │       :        :       │
//   v=0  P000 ── P100 ── P200 ── P300
//        u=0                      u=1
enum ControlPoint {
    // 境界リング（この順で一周する: u=0 辺 → v=1 辺 → u=1 辺 → v=0 辺）。
    // 一周する並びにしているのは、制御点ネットの線を連続した
    // インデックス列 (0,1),(1,2),... で描けるようにするため
    P000 = 0, P010, P020, P030,   // u=0 辺（v: 0→1）
    P130, P230,                   // v=1 辺の中間 2 点（u: 0→1）
    P330, P320, P310, P300,       // u=1 辺（v: 1→0）
    P200, P100,                   // v=0 辺の中間 2 点（u: 1→0）
    // 内部ペア（偶数が k=0、その +1 が k=1。partner = index ^ 1 で求まる並びに
    // してあるので、ペア連動編集がビット反転 1 回で書ける）
    P110, P111,
    P120, P121,
    P210, P211,
    P220, P221,
    ControlPointCount             // = 20
};

struct GregoryPatch {
    glm::vec3 p[ControlPointCount];
};

struct SurfaceSample {
    glm::vec3 position;
    glm::vec3 du;       // ∂S/∂u（解析偏微分。有理項 ∂Q/∂u を含む）
    glm::vec3 dv;       // ∂S/∂v
    glm::vec3 normal;   // normalize(du × dv)
};

// グリッド添字 (i,j,k) → ControlPoint。境界では k は無視される。
int controlIndex(int i, int j, int k);

// 表示用の名前（"P110" など）
const char* controlPointName(int index);

// 式(2.7)–(2.9) の評価
glm::vec3 position(const GregoryPatch& patch, float u, float v);
SurfaceSample sample(const GregoryPatch& patch, float u, float v);

// 4×4 Bézier 制御点グリッド（grid[i][j], i=u方向）から内部ペアを一致させた
// パッチを作る（性質3: このときGregory 曲面は Bézier 曲面に一致）
GregoryPatch fromBezierGrid(const glm::vec3 grid[4][4]);

// 起動時用: 中央が盛り上がった 3×3 スパンのパッチ
GregoryPatch defaultPatch();

} // namespace greg
