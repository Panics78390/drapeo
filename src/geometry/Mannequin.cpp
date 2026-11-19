#include "geometry/Mannequin.h"

#include <array>
#include <cmath>
#include <functional>
#include <algorithm>
#include <numbers>

namespace drapeo::geometry {

namespace {
using Fn = std::function<glm::vec3(float, float)>;

// Surface paramétrée (u autour, v le long), fermée en u, bouchée aux extrémités.
void addSurface(std::vector<glm::vec3>& P, std::vector<uint32_t>& I, const Fn& f, int nu, int nv)
{
    const auto base = static_cast<uint32_t>(P.size());
    for (int j = 0; j <= nv; ++j)
        for (int i = 0; i < nu; ++i) P.push_back(f(static_cast<float>(i) / nu, static_cast<float>(j) / nv));
    auto id = [&](int i, int j) { return base + static_cast<uint32_t>(j * nu + (i % nu)); };
    for (int j = 0; j < nv; ++j)
        for (int i = 0; i < nu; ++i) {
            const uint32_t a = id(i, j), b = id(i + 1, j), c = id(i, j + 1), d = id(i + 1, j + 1);
            I.insert(I.end(), {a, c, d, a, d, b});
        }
    for (int ring : {0, nv}) {
        glm::vec3 c(0.0f);
        for (int i = 0; i < nu; ++i) c += P[id(i, ring)];
        P.push_back(c / static_cast<float>(nu));
        const auto ci = static_cast<uint32_t>(P.size() - 1);
        for (int i = 0; i < nu; ++i) {
            if (ring == 0) I.insert(I.end(), {ci, id(i, ring), id(i + 1, ring)});
            else I.insert(I.end(), {ci, id(i + 1, ring), id(i, ring)});
        }
    }
}
} // namespace

void makeMannequin(std::vector<glm::vec3>& P, std::vector<uint32_t>& I)
{
    P.clear();
    I.clear();
    const float pi = std::numbers::pi_v<float>;
    constexpr float cm = 0.01f * 1.75f / 1.72f;   // modèle dessiné en cm, ramené à 1,75 m

    // Buste : (rayon, hauteur) en cm, section elliptique (profondeur 70 %)
    static const std::array<glm::vec2, 12> torso{{{13.0f, 80}, {17.0f, 88}, {17.5f, 95}, {15.5f, 102},
                                                  {13.5f, 108}, {14.8f, 117}, {17.0f, 126}, {17.5f, 134},
                                                  {16.5f, 140}, {11.0f, 145}, {6.0f, 148}, {5.5f, 152}}};
    addSurface(P, I, [&](float u, float v) {
        const float y = torso.front().y + v * (torso.back().y - torso.front().y);
        float r = torso.back().x;
        for (std::size_t k = 0; k + 1 < torso.size(); ++k)
            if (y >= torso[k].y && y <= torso[k + 1].y) {
                const float t = (y - torso[k].y) / (torso[k + 1].y - torso[k].y);
                r = torso[k].x + t * (torso[k + 1].x - torso[k].x);
                break;
            }
        const float a = 2 * pi * u;
        return glm::vec3(r * std::cos(a), y, 0.7f * r * std::sin(a)) * cm;
    }, 48, 40);

    auto ellipsoid = [&](glm::vec3 c, glm::vec3 r) {
        addSurface(P, I, [=](float u, float v) {
            const float th = 2 * pi * u, ph = pi * (0.02f + 0.96f * v);
            return (c + glm::vec3(r.x * std::sin(ph) * std::cos(th), -r.y * std::cos(ph), r.z * std::sin(ph) * std::sin(th))) * cm;
        }, 32, 20);
    };
    auto capsule = [&](glm::vec3 p0, glm::vec3 p1, float r0, float r1) {
        const glm::vec3 ax = glm::normalize(p1 - p0);
        const float L = glm::length(p1 - p0);
        const glm::vec3 tmp = std::abs(ax.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 0, 1);
        const glm::vec3 e1 = glm::normalize(glm::cross(ax, tmp));
        const glm::vec3 e2 = glm::cross(e1, ax);   // (e1, e2) tourne pour des normales sortantes
        addSurface(P, I, [=](float u, float v) {
            const float a = 2 * pi * u;
            float ring, along;
            if (v < 0.2f) {
                const float o = (0.2f - v) / 0.2f;
                ring = r0 * std::cos(o * pi / 2);
                along = -r0 * std::sin(o * pi / 2);
            } else if (v > 0.8f) {
                const float o = (v - 0.8f) / 0.2f;
                ring = r1 * std::cos(o * pi / 2);
                along = L + r1 * std::sin(o * pi / 2);
            } else {
                const float t = (v - 0.2f) / 0.6f;
                ring = r0 + (r1 - r0) * t;
                along = t * L;
            }
            return (p0 + ax * along + ring * (std::cos(a) * e1 + std::sin(a) * e2)) * cm;
        }, 20, 24);
    };

    ellipsoid({0, 163, 1}, {8.5f, 11, 9.5f});   // tête
    for (float sx : {-1.0f, 1.0f}) {
        capsule({sx * 17.5f, 139, 0}, {sx * 33, 116, 0}, 5.0f, 4.0f);   // bras
        capsule({sx * 33, 116, 0}, {sx * 46, 96, 3}, 4.0f, 3.0f);       // avant-bras
        capsule({sx * 9, 88, 0}, {sx * 10, 48, 0}, 8.0f, 5.5f);         // cuisse
        capsule({sx * 10, 48, 0}, {sx * 10, 6, 0}, 5.5f, 4.0f);         // mollet
    }
    // Pieds au sol : le bas du mollet arrondi descend à ~2 cm.
    float minY = 1e9f;
    for (const auto& p : P) minY = std::min(minY, p.y);
    for (auto& p : P) p.y -= minY;
}

} // namespace drapeo::geometry
