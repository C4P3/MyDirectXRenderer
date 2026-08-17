#include "core/lattice.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <utility>

#include "core/g1.h"

namespace greg {
namespace {

// パッチの辺 k（面の第 k 辺）と Edge enum・パッチ t 方向の対応。
// 面 (c0,c1,c2,c3) を patch の (u,v)=(0,0)→c0, (1,0)→c1, (1,1)→c2, (0,1)→c3
// に割り当てる。k=2,3 はパッチ t が面の辺の向きと逆になる
constexpr int kSideEdge[4] = {EdgeV0, EdgeU1, EdgeV1, EdgeU0};

struct EdgeRec {
    int a = -1, b = -1;        // 頂点（a < b）
    int face[2] = {-1, -1};
    int side[2] = {0, 0};
    int tFrom[2] = {-1, -1};   // パッチ t=0 側の頂点
    int count = 0;
    glm::vec3 c[4];            // 境界 Bézier 曲線（a → b の向きで格納）
};

int cornerOf(const glm::ivec4& face, int v) {
    for (int k = 0; k < 4; ++k)
        if (face[k] == v) return k;
    return -1;
}

} // namespace

PatchMesh roundLattice(const QuadMesh& mesh, const RoundingOptions& opt) {
    const int nF = int(mesh.faces.size());
    const int nV = int(mesh.vertices.size());
    const auto& V = mesh.vertices;

    // ---- Doo-Sabin 点（四辺形: 重み (9,3,3,1)/16。面の中点分割の面点と等価）----
    std::vector<std::array<glm::vec3, 4>> ds(nF);
    for (int f = 0; f < nF; ++f) {
        const glm::ivec4& q = mesh.faces[f];
        for (int k = 0; k < 4; ++k) {
            ds[f][k] = (9.0f * V[q[k]] + 3.0f * V[q[(k + 1) % 4]] +
                        3.0f * V[q[(k + 3) % 4]] + V[q[(k + 2) % 4]]) / 16.0f;
        }
    }

    // ---- Step 1: 格子頂点に対応する曲面上の頂点 S[v]（周囲の DS 点の平均 =
    //      Doo-Sabin メッシュ M1 の V-face の面点。XVL §3.2 Step 1）----
    std::vector<glm::vec3> S(nV, glm::vec3(0.0f));
    std::vector<glm::vec3> normal(nV, glm::vec3(0.0f));   // 接平面射影用
    std::vector<int> valence(nV, 0);
    for (int f = 0; f < nF; ++f) {
        const glm::ivec4& q = mesh.faces[f];
        const glm::vec3 fn = glm::cross(V[q[2]] - V[q[0]], V[q[3]] - V[q[1]]);
        for (int k = 0; k < 4; ++k) {
            S[q[k]] += ds[f][k];
            normal[q[k]] += fn;
            ++valence[q[k]];
        }
    }
    for (int v = 0; v < nV; ++v) {
        if (valence[v] > 0) S[v] /= float(valence[v]);
        // Rounding Weight: 丸め位置と元頂点の補間（w=1 で曲面が頂点を通る）
        if (opt.vertexWeights && v < int(opt.vertexWeights->size()))
            S[v] = glm::mix(S[v], V[v], glm::clamp((*opt.vertexWeights)[v], 0.0f, 1.0f));
    }

    // ---- 辺の収集 ----
    std::map<std::pair<int, int>, int> edgeIndex;
    std::vector<EdgeRec> edges;
    std::vector<std::array<int, 4>> faceEdge(nF);   // 面の第 k 辺 → edges 添字
    for (int f = 0; f < nF; ++f) {
        const glm::ivec4& q = mesh.faces[f];
        for (int k = 0; k < 4; ++k) {
            const int a = q[k], b = q[(k + 1) % 4];
            const auto key = std::minmax(a, b);
            auto it = edgeIndex.find(key);
            if (it == edgeIndex.end()) {
                it = edgeIndex.emplace(key, int(edges.size())).first;
                EdgeRec rec;
                rec.a = key.first;
                rec.b = key.second;
                edges.push_back(rec);
            }
            EdgeRec& rec = edges[it->second];
            if (rec.count < 2) {
                rec.face[rec.count] = f;
                rec.side[rec.count] = k;
                // パッチ t=0 の頂点: k=0,1 は辺の向きどおり、k=2,3 は逆
                rec.tFrom[rec.count] = (k < 2) ? a : b;
                ++rec.count;
            }
            faceEdge[f][k] = it->second;
        }
    }

    // ---- Step 2: 境界 Bézier 曲線。内部制御点は P1 = P0 + (4/3)(Q0 − P0)
    //      （Q0 は M1 上の辺点 = 辺の両側の DS 点の中点。XVL §3.2 Step 2）----
    for (EdgeRec& e : edges) {
        glm::vec3 Qa(0.0f), Qb(0.0f);
        for (int s = 0; s < e.count; ++s) {
            const glm::ivec4& q = mesh.faces[e.face[s]];
            Qa += ds[e.face[s]][cornerOf(q, e.a)];
            Qb += ds[e.face[s]][cornerOf(q, e.b)];
        }
        Qa /= float(e.count);
        Qb /= float(e.count);
        e.c[0] = S[e.a];
        e.c[1] = S[e.a] + (4.0f / 3.0f) * (Qa - S[e.a]);
        e.c[2] = S[e.b] + (4.0f / 3.0f) * (Qb - S[e.b]);
        e.c[3] = S[e.b];
    }

    // ---- 角の接平面射影: 頂点まわりの全接ベクトルを共通平面に乗せ、
    //      Chiyokura 補正の coplanar 前提を厳密化する ----
    if (opt.projectCornerTangents) {
        for (EdgeRec& e : edges) {
            for (int end = 0; end < 2; ++end) {
                const int v = (end == 0) ? e.a : e.b;
                const float len = glm::length(normal[v]);
                if (len < 1e-12f) continue;
                const glm::vec3 n = normal[v] / len;
                glm::vec3& p = e.c[(end == 0) ? 1 : 2];
                p -= glm::dot(p - S[v], n) * n;
            }
        }
    }

    // ---- Step 3: パッチ組み立てと Chiyokura 法による G1 内挿 ----
    PatchMesh out;
    out.patches.resize(nF);
    for (int f = 0; f < nF; ++f) {
        const glm::ivec4& q = mesh.faces[f];
        GregoryPatch& patch = out.patches[f];
        for (int k = 0; k < 4; ++k) {
            const EdgeRec& e = edges[faceEdge[f][k]];
            const EdgeDef& def = edgeDef(kSideEdge[k]);
            const int tFrom = (k < 2) ? q[k] : q[(k + 1) % 4];
            for (int i = 0; i < 4; ++i)
                patch.p[def.g[i]] = (tFrom == e.a) ? e.c[i] : e.c[3 - i];
        }
        // 内部は Coons（平行四辺形）初期値。共有辺は applyG1 が上書きする
        const auto coons = [&patch](int target0, int target1, int adjA, int adjB, int corner) {
            const glm::vec3 g = patch.p[adjA] + patch.p[adjB] - patch.p[corner];
            patch.p[target0] = g;
            patch.p[target1] = g;
        };
        coons(P110, P111, P100, P010, P000);
        coons(P210, P211, P200, P310, P300);
        coons(P120, P121, P130, P020, P030);
        coons(P220, P221, P230, P320, P330);
    }
    for (const EdgeRec& e : edges) {
        if (e.count == 2)
            out.edges.push_back({e.face[0], kSideEdge[e.side[0]],
                                 e.face[1], kSideEdge[e.side[1]],
                                 e.tFrom[0] != e.tFrom[1]});
    }
    if (opt.applyCompatibility) applyG1(out);
    return out;
}

// ---- プリミティブ -------------------------------------------------------------

QuadMesh makeCube() {
    QuadMesh m;
    m.vertices = {{-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
                  {-1, -1, 1},  {1, -1, 1},  {1, 1, 1},  {-1, 1, 1}};
    m.faces = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4},
               {3, 7, 6, 2}, {0, 4, 7, 3}, {1, 2, 6, 5}};
    return m;
}

QuadMesh makeTorus(int majorSegs, int minorSegs, float R, float r) {
    QuadMesh m;
    m.vertices.reserve(size_t(majorSegs) * minorSegs);
    for (int i = 0; i < majorSegs; ++i) {
        const float th = 2.0f * 3.14159265f * i / majorSegs;
        for (int j = 0; j < minorSegs; ++j) {
            const float ph = 2.0f * 3.14159265f * j / minorSegs;
            const float a = R + r * std::cos(ph);
            m.vertices.push_back({a * std::cos(th), r * std::sin(ph), a * std::sin(th)});
        }
    }
    const auto idx = [&](int i, int j) {
        return ((i % majorSegs) * minorSegs) + (j % minorSegs);
    };
    for (int i = 0; i < majorSegs; ++i)
        for (int j = 0; j < minorSegs; ++j)
            m.faces.push_back({idx(i, j), idx(i, j + 1), idx(i + 1, j + 1), idx(i + 1, j)});
    return m;
}

bool loadQuadObj(const char* path, QuadMesh& out) {
    std::ifstream f(path);
    if (!f) {
        std::fprintf(stderr, "obj not found: %s\n", path);
        return false;
    }
    QuadMesh m;
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string tag;
        ss >> tag;
        if (tag == "v") {
            glm::vec3 p;
            ss >> p.x >> p.y >> p.z;
            m.vertices.push_back(p);
        } else if (tag == "f") {
            glm::ivec4 q;
            int n = 0;
            std::string tok;
            while (ss >> tok) {
                if (n >= 4) {
                    std::fprintf(stderr, "obj: 四辺形以外の面があります (%s)\n", path);
                    return false;
                }
                // "v", "v/vt", "v/vt/vn" の先頭を取る（1 始まり）
                q[n++] = std::atoi(tok.c_str()) - 1;
            }
            if (n != 4) {
                std::fprintf(stderr, "obj: 四辺形以外の面があります (%s)\n", path);
                return false;
            }
            m.faces.push_back(q);
        }
    }
    for (const glm::ivec4& q : m.faces)
        for (int k = 0; k < 4; ++k)
            if (q[k] < 0 || q[k] >= int(m.vertices.size())) {
                std::fprintf(stderr, "obj: 頂点インデックスが不正です (%s)\n", path);
                return false;
            }
    out = std::move(m);
    return true;
}

} // namespace greg
