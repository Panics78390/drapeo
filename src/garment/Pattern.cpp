#include "garment/Pattern.h"

#include <algorithm>
#include <cmath>

namespace drapeo::garment {

glm::vec2 Garment::edgeStart(const EdgeRef& e) const
{
    return panels[static_cast<std::size_t>(e.panel)].points[static_cast<std::size_t>(e.edge)];
}

glm::vec2 Garment::edgeEnd(const EdgeRef& e) const
{
    const auto& pts = panels[static_cast<std::size_t>(e.panel)].points;
    return pts[(static_cast<std::size_t>(e.edge) + 1) % pts.size()];
}

float Garment::edgeLength(const EdgeRef& e) const { return glm::distance(edgeStart(e), edgeEnd(e)); }

void Garment::removeSeamsOn(const EdgeRef& e)
{
    std::erase_if(seams, [&](const Seam& s) { return s.a == e || s.b == e; });
}

void Garment::removePanel(int panel)
{
    std::erase_if(seams, [&](const Seam& s) { return s.a.panel == panel || s.b.panel == panel; });
    for (auto& s : seams) {
        if (s.a.panel > panel) --s.a.panel;
        if (s.b.panel > panel) --s.b.panel;
    }
    panels.erase(panels.begin() + panel);
}

void Garment::removePoint(int panel, int point)
{
    auto& pts = panels[static_cast<std::size_t>(panel)].points;
    if (pts.size() <= 3) return;
    const int n = static_cast<int>(pts.size());
    // Les deux bords qui touchent ce point disparaissent (fusionnés en un seul).
    removeSeamsOn({panel, (point - 1 + n) % n});
    removeSeamsOn({panel, point});
    for (auto& s : seams) {
        for (EdgeRef* e : {&s.a, &s.b}) {
            if (e->panel == panel && e->edge > point) --e->edge;
        }
    }
    pts.erase(pts.begin() + point);
}

void Garment::insertPoint(int panel, int edge, glm::vec2 position)
{
    removeSeamsOn({panel, edge});
    for (auto& s : seams) {
        for (EdgeRef* e : {&s.a, &s.b}) {
            if (e->panel == panel && e->edge > edge) ++e->edge;
        }
    }
    auto& pts = panels[static_cast<std::size_t>(panel)].points;
    pts.insert(pts.begin() + edge + 1, position);
}

int Garment::addMirrorCopy(int panel, float gap)
{
    Panel copy = panels[static_cast<std::size_t>(panel)];
    float maxX = -1e9f;
    for (const auto& p : copy.points) maxX = std::max(maxX, p.x);
    // Miroir autour d'un axe vertical placé à droite de la pièce.
    const float axis = maxX + gap * 0.5f;
    for (auto& p : copy.points) p.x = 2.0f * axis - p.x;
    std::reverse(copy.points.begin(), copy.points.end());
    copy.name += " (miroir)";
    copy.placement.angleDeg = copy.placement.angleDeg + 180.0f;
    if (copy.placement.angleDeg > 180.0f) copy.placement.angleDeg -= 360.0f;
    panels.push_back(copy);
    return static_cast<int>(panels.size()) - 1;
}

float signedArea(const std::vector<glm::vec2>& poly)
{
    float a = 0.0f;
    for (std::size_t i = 0, n = poly.size(); i < n; ++i) {
        const glm::vec2 p = poly[i], q = poly[(i + 1) % n];
        a += p.x * q.y - q.x * p.y;
    }
    return 0.5f * a;
}

bool pointInPolygon(const std::vector<glm::vec2>& poly, glm::vec2 p)
{
    bool inside = false;
    for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const glm::vec2 a = poly[i], b = poly[j];
        if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) inside = !inside;
    }
    return inside;
}

float distanceToSegment(glm::vec2 p, glm::vec2 a, glm::vec2 b, float* t)
{
    const glm::vec2 ab = b - a;
    const float len2 = glm::dot(ab, ab);
    float s = len2 > 0.0f ? glm::dot(p - a, ab) / len2 : 0.0f;
    s = std::clamp(s, 0.0f, 1.0f);
    if (t) *t = s;
    return glm::distance(p, a + ab * s);
}

glm::vec2 centroid(const std::vector<glm::vec2>& poly)
{
    glm::vec2 c(0.0f);
    for (const auto& p : poly) c += p;
    return poly.empty() ? c : c / static_cast<float>(poly.size());
}

// -----------------------------------------------------------------------------
//  Modèles. Coordonnées en mètres, pièce vue de l'extérieur, bas de la pièce à
//  y = 0. Le dos est dessiné à droite du devant dans l'éditeur 2D.
// -----------------------------------------------------------------------------
namespace {
Panel tankPanel(const char* name, float neckDepth, float extraLength, float hemHalfWidth, float xOffset)
{
    const float L = extraLength;
    Panel p;
    p.name = name;
    p.points = {{-hemHalfWidth, 0.0f},  {hemHalfWidth, 0.0f},  {0.25f, 0.30f + L}, {0.18f, 0.40f + L},
                {0.135f, 0.53f + L},    {0.075f, 0.53f + L},   {0.0f, 0.53f + L - neckDepth},
                {-0.075f, 0.53f + L},   {-0.135f, 0.53f + L},  {-0.18f, 0.40f + L}, {-0.25f, 0.30f + L}};
    for (auto& pt : p.points) pt.x += xOffset;
    return p;
}

// Indices des bords du débardeur : 1 = côté droit, 4 = épaule droite,
// 7 = épaule gauche, 10 = côté gauche (vu de l'extérieur).
void sewTank(Garment& g)
{
    g.seams.push_back({{0, 1}, {1, 10}});
    g.seams.push_back({{0, 10}, {1, 1}});
    g.seams.push_back({{0, 4}, {1, 7}});
    g.seams.push_back({{0, 7}, {1, 4}});
}
} // namespace

Garment makeTankTop()
{
    Garment g;
    Panel front = tankPanel("Devant", 0.13f, 0.0f, 0.25f, 0.0f);
    front.placement = {0.0f, 0.95f, 0.24f};
    Panel back = tankPanel("Dos", 0.05f, 0.0f, 0.25f, 0.65f);
    back.placement = {180.0f, 0.95f, 0.24f};
    front.color = back.color = {0.82f, 0.80f, 0.74f};
    g.panels = {front, back};
    sewTank(g);
    return g;
}

Garment makeDress()
{
    Garment g;
    Panel front = tankPanel("Devant", 0.13f, 0.42f, 0.36f, 0.0f);
    front.placement = {0.0f, 0.53f, 0.25f};
    Panel back = tankPanel("Dos", 0.05f, 0.42f, 0.36f, 0.85f);
    back.placement = {180.0f, 0.53f, 0.25f};
    front.color = back.color = {0.62f, 0.16f, 0.24f};
    g.panels = {front, back};
    sewTank(g);
    return g;
}

Garment makeSkirt()
{
    Garment g;
    Panel front;
    front.name = "Jupe devant";
    front.points = {{-0.30f, 0.0f}, {0.30f, 0.0f}, {0.21f, 0.50f}, {-0.21f, 0.50f}};
    front.placement = {0.0f, 0.60f, 0.25f};
    Panel back = front;
    back.name = "Jupe dos";
    for (auto& p : back.points) p.x += 0.70f;
    back.placement = {180.0f, 0.60f, 0.25f};
    front.color = back.color = {0.20f, 0.29f, 0.47f};
    g.panels = {front, back};
    // 1 = côté droit, 3 = côté gauche
    g.seams.push_back({{0, 1}, {1, 3}});
    g.seams.push_back({{0, 3}, {1, 1}});
    return g;
}

} // namespace drapeo::garment
