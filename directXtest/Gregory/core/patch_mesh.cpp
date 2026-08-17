#include "core/patch_mesh.h"

#include <glm/geometric.hpp>

namespace greg {
namespace {

constexpr EdgeDef kEdges[4] = {
    /* EdgeV0 (v=0, t=u) */ {{P000, P100, P200, P300}, P010, P310, P110, P210},
    /* EdgeV1 (v=1, t=u) */ {{P030, P130, P230, P330}, P020, P320, P120, P220},
    /* EdgeU0 (u=0, t=v) */ {{P000, P010, P020, P030}, P100, P130, P111, P121},
    /* EdgeU1 (u=1, t=v) */ {{P300, P310, P320, P330}, P200, P230, P211, P221},
};

} // namespace

const EdgeDef& edgeDef(int edge) {
    return kEdges[edge];
}

void edgeUV(int edge, float t, float& u, float& v) {
    switch (edge) {
    case EdgeV0: u = t; v = 0.0f; break;
    case EdgeV1: u = t; v = 1.0f; break;
    case EdgeU0: u = 0.0f; v = t; break;
    default:     u = 1.0f; v = t; break;
    }
}

PatchMesh meshFromLattice(int M, int N, const std::vector<glm::vec3>& lattice) {
    const int W = 3 * M + 1;
    PatchMesh mesh;
    mesh.patches.reserve(size_t(M) * N);
    for (int r = 0; r < N; ++r) {
        for (int c = 0; c < M; ++c) {
            glm::vec3 grid[4][4];
            for (int i = 0; i < 4; ++i)
                for (int j = 0; j < 4; ++j)
                    grid[i][j] = lattice[size_t(3 * r + j) * W + (3 * c + i)];
            mesh.patches.push_back(fromBezierGrid(grid));
        }
    }
    for (int r = 0; r < N; ++r) {
        for (int c = 0; c < M; ++c) {
            const int p = r * M + c;
            // 格子は全パッチ同一向きなので t の向きも一致（flip=false）
            if (c + 1 < M) mesh.edges.push_back({p, EdgeU1, p + 1, EdgeU0, false});
            if (r + 1 < N) mesh.edges.push_back({p, EdgeV1, p + M, EdgeV0, false});
        }
    }
    return mesh;
}

void smoothLatticeJunctions(int M, int N, std::vector<glm::vec3>& lattice) {
    const int W = 3 * M + 1, H = 3 * N + 1;
    auto at = [&](int x, int y) -> glm::vec3& { return lattice[size_t(y) * W + x]; };

    // 各ジャンクションが動かす隣接点は互いに素（隣のジャンクションとは 3 離れて
    // いる）なので、in-place で処理してよい
    for (int r = 0; r <= N; ++r) {
        for (int c = 0; c <= M; ++c) {
            // 共有辺の端点になるジャンクションだけ処理する
            const bool onSharedV = (c > 0 && c < M);   // 縦の共有辺 x=3c 上
            const bool onSharedH = (r > 0 && r < N);   // 横の共有辺 y=3r 上
            if (!onSharedV && !onSharedH) continue;

            const int x = 3 * c, y = 3 * r;
            const glm::vec3 P = at(x, y);
            const bool hasL = x > 0, hasR = x < W - 1;
            const bool hasD = y > 0, hasU = y < H - 1;

            const glm::vec3 dh = (hasL && hasR) ? at(x + 1, y) - at(x - 1, y)
                                 : hasR         ? at(x + 1, y) - P
                                                : P - at(x - 1, y);
            const glm::vec3 dv = (hasD && hasU) ? at(x, y + 1) - at(x, y - 1)
                                 : hasU         ? at(x, y + 1) - P
                                                : P - at(x, y - 1);
            const glm::vec3 n = glm::cross(dh, dv);
            const float len = glm::length(n);
            if (len < 1e-12f) continue;   // 退化（接平面が推定できない）
            const glm::vec3 nn = n / len;

            auto project = [&](int qx, int qy) {
                glm::vec3& q = at(qx, qy);
                q -= glm::dot(q - P, nn) * nn;
            };
            if (hasL) project(x - 1, y);
            if (hasR) project(x + 1, y);
            if (hasD) project(x, y - 1);
            if (hasU) project(x, y + 1);
        }
    }
}

} // namespace greg
