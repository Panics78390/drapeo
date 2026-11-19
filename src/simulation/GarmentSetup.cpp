#include "simulation/GarmentSetup.h"

#include <algorithm>
#include <cmath>

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
