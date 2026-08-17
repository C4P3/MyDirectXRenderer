#include "core/gregory.h"

#include <glm/geometric.hpp>
#include <cmath>

namespace greg {
namespace {

// (i,j) → 境界の ControlPoint。内部は k=0 の値を入れておき controlIndex で分岐
constexpr int kGrid[4][4][2] = {
    // j=0            j=1                j=2                j=3
    {{P000, P000}, {P010, P010}, {P020, P020}, {P030, P030}},   // i=0
    {{P100, P100}, {P110, P111}, {P120, P121}, {P130, P130}},   // i=1
    {{P200, P200}, {P210, P211}, {P220, P221}, {P230, P230}},   // i=2
    {{P300, P300}, {P310, P310}, {P320, P320}, {P330, P330}},   // i=3
};

constexpr const char* kNames[ControlPointCount] = {
    "P000", "P010", "P020", "P030", "P130", "P230",
    "P330", "P320", "P310", "P300", "P200", "P100",
    "P110", "P111", "P120", "P121", "P210", "P211", "P220", "P221",
};

void bernstein3(float t, float B[4], float dB[4]) {
    const float s = 1.0f - t;
    B[0] = s * s * s;
    B[1] = 3.0f * t * s * s;
    B[2] = 3.0f * t * t * s;
    B[3] = t * t * t;
    dB[0] = -3.0f * s * s;
    dB[1] = 3.0f * s * s - 6.0f * t * s;
    dB[2] = 6.0f * t * s - 3.0f * t * t;
    dB[3] = 3.0f * t * t;
}

// Q_ij(u,v) と ∂Q/∂u, ∂Q/∂v を 4×4 グリッドで構築する。
// 境界 (式(2.8)) は Q = P で偏微分 0。内部 (式(2.9)) は
//   Q = (a·Pij0 + b·Pij1) / (a + b),  a = i==1 ? u : 1-u,  b = j==1 ? v : 1-v
//   ∂Q/∂u = (∂a/∂u)·b·(Pij0 - Pij1) / (a+b)²   (∂a/∂u = ±1)
//   ∂Q/∂v = (∂b/∂v)·a·(Pij1 - Pij0) / (a+b)²   (∂b/∂v = ±1)
// a+b < ε ではペア平均に置換し偏微分 0（Bernstein 係数が同時に 0 になるため
// 極限値は結果に影響しない）。
void buildQ(const GregoryPatch& P, float u, float v,
            glm::vec3 Q[4][4], glm::vec3 Qu[4][4], glm::vec3 Qv[4][4]) {
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            const bool interior = (i == 1 || i == 2) && (j == 1 || j == 2);
            if (!interior) {
                Q[i][j] = P.p[kGrid[i][j][0]];
                Qu[i][j] = Qv[i][j] = glm::vec3(0.0f);
                continue;
            }
            const glm::vec3& p0 = P.p[kGrid[i][j][0]];
            const glm::vec3& p1 = P.p[kGrid[i][j][1]];
            const float a = (i == 1) ? u : 1.0f - u;
            const float b = (j == 1) ? v : 1.0f - v;
            const float den = a + b;
            if (den < kBlendEps) {
                Q[i][j] = 0.5f * (p0 + p1);
                Qu[i][j] = Qv[i][j] = glm::vec3(0.0f);
                continue;
            }
            const float sa = (i == 1) ? 1.0f : -1.0f;
            const float sb = (j == 1) ? 1.0f : -1.0f;
            const float inv2 = 1.0f / (den * den);
            Q[i][j] = (a * p0 + b * p1) / den;
            Qu[i][j] = (sa * b * inv2) * (p0 - p1);
            Qv[i][j] = (sb * a * inv2) * (p1 - p0);
        }
    }
}

} // namespace

int controlIndex(int i, int j, int k) {
    return kGrid[i][j][k];
}

const char* controlPointName(int index) {
    return (index >= 0 && index < ControlPointCount) ? kNames[index] : "?";
}

SurfaceSample sample(const GregoryPatch& patch, float u, float v) {
    glm::vec3 Q[4][4], Qu[4][4], Qv[4][4];
    buildQ(patch, u, v, Q, Qu, Qv);

    float Bu[4], dBu[4], Bv[4], dBv[4];
    bernstein3(u, Bu, dBu);
    bernstein3(v, Bv, dBv);

    SurfaceSample s{};
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            const float w = Bu[i] * Bv[j];
            s.position += w * Q[i][j];
            s.du += dBu[i] * Bv[j] * Q[i][j] + w * Qu[i][j];
            s.dv += Bu[i] * dBv[j] * Q[i][j] + w * Qv[i][j];
        }
    }
    const glm::vec3 n = glm::cross(s.du, s.dv);
    const float len = glm::length(n);
    s.normal = (len > 1e-12f) ? n / len : glm::vec3(0.0f, 1.0f, 0.0f);
    return s;
}

glm::vec3 position(const GregoryPatch& patch, float u, float v) {
    glm::vec3 Q[4][4], Qu[4][4], Qv[4][4];
    buildQ(patch, u, v, Q, Qu, Qv);

    float Bu[4], dBu[4], Bv[4], dBv[4];
    bernstein3(u, Bu, dBu);
    bernstein3(v, Bv, dBv);

    glm::vec3 pos(0.0f);
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            pos += Bu[i] * Bv[j] * Q[i][j];
    return pos;
}

GregoryPatch fromBezierGrid(const glm::vec3 grid[4][4]) {
    GregoryPatch patch{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            for (int k = 0; k < 2; ++k)
                patch.p[kGrid[i][j][k]] = grid[i][j];
    return patch;
}

GregoryPatch defaultPatch() {
    // x を j（v 方向）、z を i（u 方向）に割り当てると法線 du×dv が +y を向く
    glm::vec3 grid[4][4];
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            const float x = (j - 1.5f);
            const float z = (i - 1.5f);
            const float y = 1.6f * std::exp(-(x * x + z * z) * 0.45f);
            grid[i][j] = glm::vec3(x, y, z);
        }
    }
    return fromBezierGrid(grid);
}

} // namespace greg
