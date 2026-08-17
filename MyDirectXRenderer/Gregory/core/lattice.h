#pragma once
#include <glm/vec4.hpp>
#include <vector>

#include "core/patch_mesh.h"

namespace greg {

// XVL の Lattice Mesh（Wakita et al. 2000）。四辺形面のみ、面は外向き CCW
struct QuadMesh {
    std::vector<glm::vec3> vertices;
    std::vector<glm::ivec4> faces;
};

struct RoundingOptions {
    // 頂点ごとの丸め重み（XVL §3.2 Rounding Weight）。0 = 完全に丸める、
    // 1 = 曲面が元の格子頂点を補間（シャープ化）。nullptr なら全て 0
    const std::vector<float>* vertexWeights = nullptr;
    // 角（格子頂点）の接ベクトルを共通の接平面へ射影する。
    // Chiyokura 補正の前提（式(2.12) の共面条件）は、価数 3（V-face が三角形）と
    // 価数 4（V-face 四辺形の辺中点 = Varignon の平行四辺形）では任意の形状で
    // 厳密に成立するため射影は不要。価数 5 以上の特異頂点でのみ意味を持つ
    // （Phase 2 の smoothLatticeJunctions と同じ発想。test_lattice.cpp 参照）
    bool projectCornerTangents = true;
    // Chiyokura 法（applyG1）による内部制御点の計算。OFF だと Coons 初期値のまま
    bool applyCompatibility = true;
};

// XVL ラウンディング（XVL §3.2 の 3 ステップ）: Lattice Mesh → Gregory パッチ網
//   1. Doo-Sabin 分割 1 回分の点から、格子頂点に対応する曲面上の頂点を計算
//   2. 各格子辺に 3次 Bézier 境界曲線を生成（P1 = P0 + (4/3)(Q0−P0)）
//   3. Chiyokura 法で境界曲線に囲まれた領域を Gregory パッチで G1 内挿
// パッチは faces と同じ並び。閉メッシュなら全共有辺が SharedEdge になる
PatchMesh roundLattice(const QuadMesh& mesh, const RoundingOptions& opt = {});

// デモ用プリミティブ（外向き CCW の閉メッシュ）
QuadMesh makeCube();                                             // 価数 3（特異頂点）
QuadMesh makeTorus(int majorSegs, int minorSegs, float R, float r);   // 価数 4

// 四辺形 OBJ ローダ（v / f のみ解釈。四辺形以外の面があると false）
bool loadQuadObj(const char* path, QuadMesh& out);

} // namespace greg
