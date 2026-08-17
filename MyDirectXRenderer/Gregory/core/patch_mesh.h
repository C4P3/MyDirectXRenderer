#pragma once
#include <vector>

#include "core/gregory.h"

namespace greg {

// パッチの 4 辺。辺上のパラメータ t ∈ [0,1] の向きは EdgeDef::g の並び順
enum Edge { EdgeV0 = 0, EdgeV1, EdgeU0, EdgeU1 };

// 1 辺に関係する制御点のインデックス表
struct EdgeDef {
    int g[4];     // 境界制御点（t=0 → t=1 の順）
    int n0, n3;   // 両角の隣接境界点（cross ベクトル A0 = p[n0]−p[g[0]] 等に使う）
    int i1, i2;   // この辺の cross 微分を司る内部ペアメンバー（G1 補正の書き込み先。
                  // v 辺は k=0 側、u 辺は k=1 側が辺上で生き残る。gregory.h 参照）
};
const EdgeDef& edgeDef(int edge);

// (edge, t) → パッチの (u, v)
void edgeUV(int edge, float t, float& u, float& v);

// 境界曲線を共有する 2 パッチ（DESIGN §3.3）
struct SharedEdge {
    int patchA, edgeA;
    int patchB, edgeB;
    bool flip;    // true: B 側の t が A 側と逆向き
};

struct PatchMesh {
    std::vector<GregoryPatch> patches;
    std::vector<SharedEdge> edges;
};

// (3M+1)×(3N+1) の制御格子（行優先, index = y*(3M+1)+x）から
// M×N パッチ（x 方向 M 枚, y 方向 N 枚）のメッシュを作る。
// 各パッチは Bézier 初期化（内部ペア一致）で、隣接パッチとは C0 接続。
// パッチ (r,c) は patches[r*M+c]、grid[i][j] = lattice[(3r+j)*(3M+1) + 3c+i]（i=u, j=v）
PatchMesh meshFromLattice(int M, int N, const std::vector<glm::vec3>& lattice);

// G1 補正の前提（式(2.12): 角で B0 が span{A0, C0} に乗る）を厳密に成立させる
// プリパス。共有辺の端点となるジャンクション格子点ごとに接平面を推定し、
// 隣接 4 点（境界上は存在する分）をその平面へ射影する。
// これを通した格子は applyG1 後に角も含め全共有辺で厳密 G1 になる
void smoothLatticeJunctions(int M, int N, std::vector<glm::vec3>& lattice);

} // namespace greg
