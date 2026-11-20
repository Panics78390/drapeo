#include "simulation/GarmentSetup.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace drapeo::simulation {

void applyShrinkage(XpbdSolver& solver, const garment::Garment& g, const garment::GarmentMesh& gm)
{
    const auto& pov = gm.panelOfVertex;
    const auto& uv = gm.mesh.uvs;
    const int np = static_cast<int>(g.panels.size());
    bool any = false;
    for (const auto& p : g.panels) any = any || p.shrinkWeft != 1.0f || p.shrinkWarp != 1.0f;
    if (!any || uv.size() != pov.size()) return;
    solver.scaleRestLengths([&](uint32_t a, uint32_t b) {
        if (a >= pov.size() || b >= pov.size() || pov[a] != pov[b] || pov[a] < 0 || pov[a] >= np) return 1.0f;
        const auto& pn = g.panels[static_cast<std::size_t>(pov[a])];
        const glm::vec2 d = uv[b] - uv[a];
        const float len = glm::length(d);
        if (len < 1e-7f) return 1.0f;
        const float gr = glm::radians(pn.grainDeg);
        const glm::vec2 warp(std::cos(gr), std::sin(gr)), weft(-warp.y, warp.x);
        const glm::vec2 s(glm::dot(d, warp) * pn.shrinkWarp, glm::dot(d, weft) * pn.shrinkWeft);
        return glm::length(s) / len;
    });
}

void applyFolds(XpbdSolver& solver, const garment::GarmentMesh& gm)
{
    if (gm.foldEdges.empty()) return;
    const auto& idx = gm.mesh.indices;
    const auto& uv = gm.mesh.uvs;
    auto key = [](uint32_t a, uint32_t b) { return (static_cast<uint64_t>(std::min(a, b)) << 32) | std::max(a, b); };
    std::unordered_map<uint64_t, std::vector<uint32_t>> opposite;   // arête -> sommets opposés
    for (const auto& f : gm.foldEdges) opposite[key(f.a, f.b)];
    for (std::size_t t = 0; t + 2 < idx.size(); t += 3)
        for (int j = 0; j < 3; ++j) {
            const uint32_t a = idx[t + static_cast<std::size_t>(j)], b = idx[t + static_cast<std::size_t>((j + 1) % 3)],
                           c = idx[t + static_cast<std::size_t>((j + 2) % 3)];
            if (const auto it = opposite.find(key(a, b)); it != opposite.end()) it->second.push_back(c);
        }
    std::vector<XpbdSolver::FoldConstraint> folds;
    std::unordered_map<uint32_t, float> push;
    for (const auto& f : gm.foldEdges) {
        const auto& opp = opposite[key(f.a, f.b)];
        if (opp.size() != 2 || f.a >= uv.size() || f.b >= uv.size()) continue;
        const uint32_t p = opp[0], q = opp[1];
        // Longueur entre les deux sommets opposés une fois le tissu plié de l'angle voulu autour de l'arête.
        const glm::vec2 e = uv[f.b] - uv[f.a];
        const float el = glm::length(e);
        if (el < 1e-7f) continue;
        const glm::vec2 dir = e / el;
        auto split = [&](uint32_t v, float& along, float& height) {
            const glm::vec2 d = uv[v] - uv[f.a];
            along = glm::dot(d, dir);
            height = std::abs(d.x * dir.y - d.y * dir.x);
        };
        float ap = 0, hp = 0, aq = 0, hq = 0;
        split(p, ap, hp);
        split(q, aq, hq);
        const float phi = glm::radians(std::min(std::abs(f.angleDeg), 175.0f));
        const float rest = std::sqrt(std::max(0.0f, (ap - aq) * (ap - aq) + hp * hp + hq * hq + 2.0f * hp * hq * std::cos(phi)));
        folds.push_back({p, q, rest, f.strength});
        // Côtés repliés vers l'endroit (angle > 0) : la ligne de pli recule.
        const float sign = f.angleDeg > 0.0f ? -1.0f : 1.0f;
        if (f.angleDeg != 0.0f) push[f.a] = push[f.b] = sign;
    }
    std::vector<std::pair<uint32_t, float>> pushList(push.begin(), push.end());
    solver.setFolds(folds, pushList);
}

void applyPanelProperties(XpbdSolver& solver, const garment::Garment& g, const garment::GarmentMesh& gm)
{
    const auto& pov = gm.panelOfVertex;
    const int np = static_cast<int>(g.panels.size());
    auto panelOf = [&](uint32_t v) -> const garment::Panel* {
        const int p = v < pov.size() ? pov[v] : -1;
        return p >= 0 && p < np ? &g.panels[static_cast<std::size_t>(p)] : nullptr;
    };
    const auto n = static_cast<uint32_t>(std::min(pov.size(), solver.positions().size()));
    std::vector<float> stiffness(n, 1.0f), extra(n, 0.0f), pressure(n, 0.0f);
    std::vector<int> layer(n, 0);
    bool anyStiff = false, anyExtra = false, anyLayer = false, anyPressure = false;
    for (uint32_t i = 0; i < n; ++i) {
        const garment::Panel* p = panelOf(i);
        const bool frozen = p != nullptr && p->frozen;
        if (solver.isPinned(i) != frozen) solver.setPinned(i, frozen);   // pièce figée : particules immobiles
        if (p == nullptr) continue;
        stiffness[i] = std::max(1.0f, p->stiffness);
        extra[i] = p->thickCollision;
        layer[i] = p->layer;
        pressure[i] = p->pressure;
        anyStiff = anyStiff || stiffness[i] > 1.0f;
        anyExtra = anyExtra || extra[i] > 0.0f;
        anyLayer = anyLayer || layer[i] != 0;
        anyPressure = anyPressure || pressure[i] != 0.0f;
    }
    solver.setStiffnessScale(anyStiff ? std::move(stiffness) : std::vector<float>{});
    solver.setParticleProperties(anyExtra ? std::move(extra) : std::vector<float>{}, anyLayer ? std::move(layer) : std::vector<int>{},
                                 anyPressure ? std::move(pressure) : std::vector<float>{});
}

} // namespace drapeo::simulation
