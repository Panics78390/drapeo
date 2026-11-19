#include "garment/BodyAxes.h"

#include <algorithm>
#include <cmath>

namespace drapeo::garment {

namespace {
/// Direction principale (ACP) d'un nuage de points, par itération de puissance.
glm::vec3 principalAxis(const std::vector<glm::vec3>& pts, const glm::vec3& c)
{
    glm::mat3 cov(0.0f);
    for (const auto& p : pts) {
        const glm::vec3 d = p - c;
        cov += glm::outerProduct(d, d);
    }
    glm::vec3 v(1.0f, -1.0f, 0.0f);
    for (int i = 0; i < 50; ++i) {
        v = cov * v;
        const float l = glm::length(v);
        if (l < 1e-12f) return {0, -1, 0};
        v /= l;
    }
    return v;
}
} // namespace

BodyAxes estimateBodyAxes(const std::vector<glm::vec3>& positions)
{
    BodyAxes def;
    if (positions.size() < 100) return def;
    float maxY = 0.0f;
    for (const auto& p : positions) maxY = std::max(maxY, p.y);
    if (maxY < 0.3f) return def;
    const float s = 1.75f / maxY;   // tout ramener à 1,75 m

    BodyAxes out = def;
    // Buste : centre des sommets proches de l'axe, à hauteur de poitrine / taille.
    {
        glm::vec3 c(0.0f);
        int n = 0;
        for (const auto& q : positions) {
            const glm::vec3 p = q * s;
            if (p.y > 1.0f && p.y < 1.30f && std::abs(p.x) < 0.16f) {
                c += p;
                ++n;
            }
        }
        if (n > 20) out.torsoCenter = glm::vec3(c.x / n, 0.0f, c.z / n);
    }
    const glm::vec3 tc = out.torsoCenter;

    for (int side = 0; side < 2; ++side) {
        const float sx = side == 0 ? 1.0f : -1.0f;
        // --- Bras : sommets écartés du buste, au-dessus des hanches ---------
        std::vector<glm::vec3> arm;
        for (const auto& q : positions) {
            const glm::vec3 p = q * s;
            if (sx * (p.x - tc.x) > 0.24f && p.y > 0.85f) arm.push_back(p);
        }
        if (arm.size() > 30) {
            glm::vec3 c(0.0f);
            for (const auto& p : arm) c += p;
            c /= static_cast<float>(arm.size());
            glm::vec3 d = principalAxis(arm, c);
            if (sx * d.x < 0.0f) d = -d;   // vers la main
            if (std::abs(d.x) > 0.2f) {
                float tipT = -1e9f;
                for (const auto& p : arm) tipT = std::max(tipT, glm::dot(p - c, d));
                const float shoulderT = (tc.x + sx * 0.175f - c.x) / d.x;
                const glm::vec3 top = c + d * shoulderT;
                const glm::vec3 bottom = c + d * (tipT - 0.17f);   // poignet : bout des doigts - main
                if (glm::length(bottom - top) > 0.3f && top.y > 1.1f && top.y < 1.65f) {
                    out.armTop[side] = top;
                    out.armBottom[side] = bottom;
                }
            }
        }
        // --- Jambe : centres d'une tranche haute (cuisse) et basse (tibia) ---
        glm::vec3 hi(0.0f), lo(0.0f);
        int nh = 0, nl = 0;
        for (const auto& q : positions) {
            const glm::vec3 p = q * s;
            const float dx = sx * (p.x - tc.x);
            if (dx < 0.01f || dx > 0.30f) continue;
            if (p.y > 0.62f && p.y < 0.76f) { hi += p; ++nh; }
            if (p.y > 0.14f && p.y < 0.28f) { lo += p; ++nl; }
        }
        if (nh > 10 && nl > 10) {
            hi /= static_cast<float>(nh);
            lo /= static_cast<float>(nl);
            const glm::vec3 d = hi - lo;
            if (d.y > 0.2f) {
                out.legTop[side] = lo + d * ((0.98f - lo.y) / d.y);
                out.legBottom[side] = lo + d * ((0.04f - lo.y) / d.y);
            }
        }
    }
    return out;
}

} // namespace drapeo::garment
