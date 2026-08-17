#include "core/mismatch.h"

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cmath>

namespace greg {

MismatchStats measureNormalMismatch(const PatchMesh& mesh, int segmentsPerEdge,
                                    std::vector<MismatchSample>* samples) {
    const int segments = std::max(1, segmentsPerEdge);
    MismatchStats stats;
    double sumDegrees = 0.0;   // 辺数が多いと float の加算誤差が積むので double で足す

    if (samples) samples->reserve(samples->size() + mesh.edges.size() * (segments + 1));

    for (const SharedEdge& e : mesh.edges) {
        const GregoryPatch& A = mesh.patches[e.patchA];
        const GregoryPatch& B = mesh.patches[e.patchB];
        for (int k = 0; k <= segments; ++k) {
            const float t = float(k) / segments;
            // 同じ幾何点を指すように、B 側は flip なら t を反転して評価する
            float ua, va, ub, vb;
            edgeUV(e.edgeA, t, ua, va);
            edgeUV(e.edgeB, e.flip ? 1.0f - t : t, ub, vb);
            const SurfaceSample sa = sample(A, ua, va);
            const SurfaceSample sb = sample(B, ub, vb);

            // 数値誤差で |dot| が 1 をわずかに超えると acos が NaN になるので clamp
            const float cosine = glm::clamp(glm::dot(sa.normal, sb.normal), -1.0f, 1.0f);
            const float degrees = glm::degrees(std::acos(cosine));

            stats.maxDegrees = std::max(stats.maxDegrees, degrees);
            sumDegrees += degrees;
            ++stats.sampleCount;
            if (samples) samples->push_back({sa.position, sa.normal, degrees});
        }
    }
    stats.meanDegrees = stats.sampleCount ? float(sumDegrees / stats.sampleCount) : 0.0f;
    return stats;
}

} // namespace greg
