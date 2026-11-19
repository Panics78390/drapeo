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

const char* bodyZoneName(BodyZone z)
{
    switch (z) {
    case BodyZone::Torso: return "Buste";
    case BodyZone::LeftArm: return "Bras gauche";
    case BodyZone::RightArm: return "Bras droit";
    case BodyZone::LeftLeg: return "Jambe gauche";
    case BodyZone::RightLeg: return "Jambe droite";
    }
    return "?";
}

std::vector<EdgeRef> Garment::seamEdges(const Seam& s, int side) const
{
    const EdgeRef start = side == 0 ? s.a : s.b;
    const int count = side == 0 ? s.countA : s.countB;
    std::vector<EdgeRef> out;
    if (start.panel < 0 || start.panel >= static_cast<int>(panels.size())) return out;
    const int n = static_cast<int>(panels[static_cast<std::size_t>(start.panel)].points.size());
    for (int k = 0; k < std::min(count, n); ++k) out.push_back({start.panel, (start.edge + k) % n});
    return out;
}

float Garment::seamLength(const Seam& s, int side) const
{
    float len = 0.0f;
    for (const auto& e : seamEdges(s, side)) len += edgeLength(e);
    return len;
}

bool Garment::extendSeam(int seam, int side, int delta, bool atStart)
{
    if (seam < 0 || seam >= static_cast<int>(seams.size())) return false;
    Seam& s = seams[static_cast<std::size_t>(seam)];
    int& count = side == 0 ? s.countA : s.countB;
    const EdgeRef start = side == 0 ? s.a : s.b;
    const int n = static_cast<int>(panels[static_cast<std::size_t>(start.panel)].points.size());
    if (count + delta < 1 || count + delta >= n) return false;
    if (delta > 0) {
        // Le nouveau bord ne doit appartenir à aucune autre couture.
        const EdgeRef next{start.panel, atStart ? (start.edge - 1 + n) % n : (start.edge + count) % n};
        for (int t = 0; t < static_cast<int>(seams.size()); ++t)
            for (int sd = 0; sd < 2; ++sd)
                for (const auto& e : seamEdges(seams[static_cast<std::size_t>(t)], sd))
                    if (e == next) return false;
    }
    count += delta;
    if (atStart) (side == 0 ? s.a : s.b).edge = (start.edge - delta + n) % n;
    return true;
}

void Garment::removeSeamsOn(const EdgeRef& e)
{
    std::erase_if(seams, [&](const Seam& s) {
        for (int side = 0; side < 2; ++side)
            for (const auto& x : seamEdges(s, side))
                if (x == e) return true;
        return false;
    });
}

namespace {
/// Renumérote les bords d'une pièce dont le contour change. `map(e)` donne les
/// nouveaux bords issus de l'ancien bord e. Une couture dont un côté n'est plus
/// une suite continue est supprimée.
template <typename Map>
void remapSeams(Garment& g, int panel, int oldN, int newN, Map map)
{
    std::erase_if(g.seams, [&](Seam& s) {
        for (int side = 0; side < 2; ++side) {
            EdgeRef& start = side == 0 ? s.a : s.b;
            int& count = side == 0 ? s.countA : s.countB;
            if (start.panel != panel) continue;
            std::vector<bool> in(static_cast<std::size_t>(newN), false);
            int total = 0;
            for (int k = 0; k < std::min(count, oldN); ++k)
                for (const int e : map((start.edge + k) % oldN))
                    if (e >= 0 && e < newN && !in[static_cast<std::size_t>(e)]) {
                        in[static_cast<std::size_t>(e)] = true;
                        ++total;
                    }
            if (total == 0 || total >= newN) return true;
            int first = -1;
            for (int e = 0; e < newN; ++e)
                if (in[static_cast<std::size_t>(e)] && !in[static_cast<std::size_t>((e - 1 + newN) % newN)]) {
                    if (first >= 0) return true;   // plusieurs morceaux : couture rompue
                    first = e;
                }
            start.edge = first;
            count = total;
        }
        return false;
    });
}
} // namespace

void taperPanel(Garment& g, int panel, float amount, TaperMode mode, TaperAnchor anchor)
{
    if (panel < 0 || panel >= static_cast<int>(g.panels.size())) return;
    auto& pts = g.panels[static_cast<std::size_t>(panel)].points;
    if (pts.size() < 3) return;
    glm::vec2 lo(1e9f), hi(-1e9f);
    for (const auto& q : pts) {
        lo = glm::min(lo, q);
        hi = glm::max(hi, q);
    }
    const float ax = anchor == TaperAnchor::Left ? lo.x : anchor == TaperAnchor::Right ? hi.x : (lo.x + hi.x) * 0.5f;
    const float h = std::max(hi.y - lo.y, 1e-6f);
    auto map = [&](glm::vec2 q) {
        const float t = std::clamp((q.y - lo.y) / h, 0.0f, 1.0f);   // 0 = bas, 1 = haut
        float w = 1.0f;
        switch (mode) {
        case TaperMode::Uniform: w = 1.0f; break;
        case TaperMode::Bottom: w = 1.0f - t; break;
        case TaperMode::Middle: w = 1.0f - std::abs(2.0f * t - 1.0f); break;
        case TaperMode::Top: w = t; break;
        }
        const float f = std::max(0.1f, 1.0f + amount * w);
        return glm::vec2(ax + (q.x - ax) * f, q.y);
    };
    for (auto& q : pts) q = map(q);
    for (auto& a : g.attachments)
        if (a.base == panel) a.at = map(a.at);
}

void grainLine(const Panel& panel, glm::vec2& a, glm::vec2& b)
{
    const float r = glm::radians(panel.grainDeg);
    const glm::vec2 d(std::cos(r), std::sin(r));
    const glm::vec2 c = panel.points.size() >= 3 ? centroid(panel.points) : glm::vec2(0.0f);
    float lo = 0.0f, hi = 0.0f;
    for (const auto& q : panel.points) {
        const float t = glm::dot(q - c, d);
        lo = std::min(lo, t);
        hi = std::max(hi, t);
    }
    a = c + d * (lo * 0.66f);
    b = c + d * (hi * 0.66f);
}

int topEdge(const std::vector<glm::vec2>& poly)
{
    const int n = static_cast<int>(poly.size());
    int best = 0;
    float bestY = -1e9f;
    for (int pass = 0; pass < 2 && bestY < -1e8f; ++pass)
        for (int e = 0; e < n; ++e) {
            const glm::vec2 a = poly[static_cast<std::size_t>(e)], b = poly[static_cast<std::size_t>((e + 1) % n)];
            if (pass == 0 && std::abs(b.x - a.x) < std::abs(b.y - a.y)) continue;   // bord trop vertical
            const float y = (a.y + b.y) * 0.5f;
            if (y > bestY) {
                bestY = y;
                best = e;
            }
        }
    return best;
}

void Garment::setAttachKind(Attachment& a, AttachKind kind) const
{
    if (a.patch < 0 || a.patch >= static_cast<int>(panels.size())) return;
    const auto& pts = panels[static_cast<std::size_t>(a.patch)].points;
    const int n = static_cast<int>(pts.size());
    const int top = topEdge(pts);
    switch (kind) {
    case AttachKind::Pocket: a.startEdge = (top + 1) % n; a.count = n - 1; break;
    case AttachKind::Flap: a.startEdge = top; a.count = 1; break;
    case AttachKind::AllRound: a.startEdge = 0; a.count = 0; break;
    }
}

AttachKind Garment::attachKind(const Attachment& a) const
{
    if (a.count <= 0) return AttachKind::AllRound;
    return a.count == 1 ? AttachKind::Flap : AttachKind::Pocket;
}

int Garment::attachPatch(int patch, int base, glm::vec2 at, AttachKind kind)
{
    const int n = static_cast<int>(panels.size());
    if (patch < 0 || base < 0 || patch >= n || base >= n || patch == base) return -1;
    if (attachmentOf(base) >= 0) return -1;   // pas de poche sur une poche
    // Une pièce de base ne peut pas devenir elle-même une poche.
    for (const auto& a : attachments)
        if (a.base == patch) return -1;
    // Les coutures ordinaires de la poche sont remplacées par la pose.
    std::erase_if(seams, [&](const Seam& s) { return s.a.panel == patch || s.b.panel == patch; });
    Attachment a;
    a.patch = patch;
    a.base = base;
    a.at = at;
    setAttachKind(a, kind);
    if (const int old = attachmentOf(patch); old >= 0) {
        attachments[static_cast<std::size_t>(old)] = a;
        return old;
    }
    attachments.push_back(a);
    return static_cast<int>(attachments.size()) - 1;
}

int Garment::attachmentOf(int panel) const
{
    for (std::size_t i = 0; i < attachments.size(); ++i)
        if (attachments[i].patch == panel) return static_cast<int>(i);
    return -1;
}

std::vector<int> Garment::attachedEdges(const Attachment& a) const
{
    std::vector<int> out;
    if (a.patch < 0 || a.patch >= static_cast<int>(panels.size())) return out;
    const int n = static_cast<int>(panels[static_cast<std::size_t>(a.patch)].points.size());
    const int count = a.count <= 0 || a.count > n ? n : a.count;
    for (int k = 0; k < count; ++k) out.push_back(((a.startEdge % n) + n + k) % n);
    return out;
}

void Garment::removePanel(int panel)
{
    std::erase_if(seams, [&](const Seam& s) { return s.a.panel == panel || s.b.panel == panel; });
    for (auto& s : seams) {
        if (s.a.panel > panel) --s.a.panel;
        if (s.b.panel > panel) --s.b.panel;
    }
    std::erase_if(attachments, [&](const Attachment& a) { return a.patch == panel || a.base == panel; });
    for (auto& a : attachments) {
        if (a.patch > panel) --a.patch;
        if (a.base > panel) --a.base;
    }
    panels.erase(panels.begin() + panel);
}

void Garment::removePoint(int panel, int point)
{
    auto& pts = panels[static_cast<std::size_t>(panel)].points;
    if (pts.size() <= 3) return;
    const int n = static_cast<int>(pts.size());
    // Les deux bords qui touchent ce point fusionnent en un seul.
    remapSeams(*this, panel, n, n - 1, [&](int e) -> std::vector<int> {
        if (point == 0) return {e == 0 ? n - 2 : e - 1};
        if (e < point) return {e};
        return {e - 1};
    });
    for (auto& a : attachments)
        if (a.patch == panel && a.count > 0) {
            if (point > a.startEdge && point < a.startEdge + a.count) a.count = std::max(1, a.count - 1);
            else if (point <= a.startEdge && a.startEdge > 0) --a.startEdge;
        }
    pts.erase(pts.begin() + point);
}

void Garment::insertPoint(int panel, int edge, glm::vec2 position)
{
    auto& pts = panels[static_cast<std::size_t>(panel)].points;
    const int n = static_cast<int>(pts.size());
    // Le bord coupé devient deux bords : une couture qui le contenait les garde tous les deux.
    remapSeams(*this, panel, n, n + 1, [&](int e) -> std::vector<int> {
        if (e < edge) return {e};
        if (e == edge) return {e, e + 1};
        return {e + 1};
    });
    for (auto& a : attachments)
        if (a.patch == panel && a.count > 0) {
            if (edge < a.startEdge) ++a.startEdge;
            else if (edge < a.startEdge + a.count) ++a.count;
        }
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
    copy.grainDeg = 180.0f - copy.grainDeg;   // le droit fil est retourné comme la pièce
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
