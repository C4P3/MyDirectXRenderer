#include "core/g1.h"

#include <glm/geometric.hpp>
#include <cmath>

namespace greg {
namespace {

// B = k·A + h·C を満たす (k, h) を法線方程式で解く（B が span{A,C} 上なら厳密解、
// そうでなければ最小二乗解）
void solveKH(const glm::vec3& A, const glm::vec3& C, const glm::vec3& B,
             float& k, float& h) {
    const float aa = glm::dot(A, A), ac = glm::dot(A, C), cc = glm::dot(C, C);
    const float ab = glm::dot(A, B), cb = glm::dot(C, B);
    const float det = aa * cc - ac * ac;
    if (std::abs(det) < 1e-12f) {
        // A と C が平行（退化辺）: A 成分のみで近似
        k = (aa > 1e-12f) ? ab / aa : 0.0f;
        h = 0.0f;
        return;
    }
    k = (ab * cc - cb * ac) / det;
    h = (cb * aa - ab * ac) / det;
}

} // namespace

void applyG1(GregoryPatch& A, int edgeA, GregoryPatch& B, int edgeB, bool flip) {
    const EdgeDef& ea = edgeDef(edgeA);
    const EdgeDef& eb = edgeDef(edgeB);

    // B 側のインデックスを A の t 方向に揃える
    const int nB0 = flip ? eb.n3 : eb.n0;
    const int nB3 = flip ? eb.n0 : eb.n3;
    const int iB1 = flip ? eb.i2 : eb.i1;
    const int iB2 = flip ? eb.i1 : eb.i2;

    // 共有境界曲線（C0 接続前提なので A 側の値を使う）
    const glm::vec3 G0 = A.p[ea.g[0]], G1 = A.p[ea.g[1]];
    const glm::vec3 G2 = A.p[ea.g[2]], G3 = A.p[ea.g[3]];
    const glm::vec3 C0 = G1 - G0, C1 = G2 - G1, C2 = G3 - G2;

    // A 側 cross ベクトルと仮定 (2.10): A1 = (2A0+A3)/3, A2 = (A0+2A3)/3
    const glm::vec3 A0 = A.p[ea.n0] - G0;
    const glm::vec3 A3 = A.p[ea.n3] - G3;
    const glm::vec3 A1 = (2.0f * A0 + A3) / 3.0f;
    const glm::vec3 A2 = (A0 + 2.0f * A3) / 3.0f;
    A.p[ea.i1] = G1 + A1;
    A.p[ea.i2] = G2 + A2;

    // 角の条件 (2.12) から k, h を解く
    const glm::vec3 B0 = B.p[nB0] - G0;
    const glm::vec3 B3 = B.p[nB3] - G3;
    float k0, h0, k1, h1;
    solveKH(A0, C0, B0, k0, h0);
    solveKH(A3, C2, B3, k1, h1);

    // (2.11)
    const glm::vec3 B1 =
        ((k1 - k0) * A0 + 3.0f * k0 * A1 + 2.0f * h0 * C1 + h1 * C0) / 3.0f;
    const glm::vec3 B2 =
        (3.0f * k1 * A2 - (k1 - k0) * A3 + h0 * C2 + 2.0f * h1 * C1) / 3.0f;
    B.p[iB1] = G1 + B1;
    B.p[iB2] = G2 + B2;
}

void applyG1(PatchMesh& mesh) {
    // 各内部ペアメンバーはちょうど 1 つの辺に属するため（gregory.h の規約）、
    // 辺ごとの書き込みは互いに干渉せず、適用順序に依存しない
    for (const SharedEdge& e : mesh.edges)
        applyG1(mesh.patches[e.patchA], e.edgeA, mesh.patches[e.patchB], e.edgeB, e.flip);
}

} // namespace greg
