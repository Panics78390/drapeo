#include "garment/PatternOps.h"

#include "garment/GarmentMesher.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace drapeo::garment {

namespace {

int indexIn(const std::vector<int>& ids, int p)
{
    const auto it = std::find(ids.begin(), ids.end(), p);
    return it == ids.end() ? -1 : static_cast<int>(it - ids.begin());
}

void bounds(const Garment& g, const std::vector<int>& ids, glm::vec2& lo, glm::vec2& hi)
{
    lo = glm::vec2(1e9f);
    hi = glm::vec2(-1e9f);
    for (const int p : ids)
        for (const auto& q : g.panels[static_cast<std::size_t>(p)].points) {
            lo = glm::min(lo, q);
            hi = glm::max(hi, q);
        }
}

std::vector<int> validIds(const Garment& g, const std::vector<int>& ids)
{
    std::vector<int> out;
    for (const int p : ids)
        if (p >= 0 && p < static_cast<int>(g.panels.size()) && indexIn(out, p) < 0) out.push_back(p);
    return out;
}

float wrapDeg(float a)
{
    while (a > 180.0f) a -= 360.0f;
    while (a <= -180.0f) a += 360.0f;
    return a;
}

BodyZone mirroredZone(BodyZone z)
{
    switch (z) {
    case BodyZone::LeftArm: return BodyZone::RightArm;
    case BodyZone::RightArm: return BodyZone::LeftArm;
    case BodyZone::LeftLeg: return BodyZone::RightLeg;
    case BodyZone::RightLeg: return BodyZone::LeftLeg;
    default: return z;
    }
}

/// Remet les pièces dans l'ordre `order` (order[k] = ancien index de la pièce k).
void permute(Garment& g, const std::vector<int>& order)
{
    std::vector<int> newOf(order.size());
    for (std::size_t k = 0; k < order.size(); ++k) newOf[static_cast<std::size_t>(order[k])] = static_cast<int>(k);
    std::vector<Panel> panels;
    panels.reserve(order.size());
    for (const int o : order) panels.push_back(g.panels[static_cast<std::size_t>(o)]);
    g.panels = std::move(panels);
    for (auto& s : g.seams) {
        s.a.panel = newOf[static_cast<std::size_t>(s.a.panel)];
        s.b.panel = newOf[static_cast<std::size_t>(s.b.panel)];
    }
    for (auto& a : g.attachments) {
        a.patch = newOf[static_cast<std::size_t>(a.patch)];
        a.base = newOf[static_cast<std::size_t>(a.base)];
    }
}

/// Matrice 2x2 de linkTransform appliquée à un vecteur, et son inverse.
glm::vec2 inverseLink(glm::vec2 v, float angleDeg, bool mirror)
{
    const glm::vec2 r = linkTransform(v, -angleDeg, false);
    return mirror ? glm::vec2(-r.x, r.y) : r;
}

bool rigidlyEqual(const std::vector<glm::vec2>& a, const std::vector<glm::vec2>& b)
{
    if (a.size() != b.size() || a.size() < 3) return false;
    const std::size_t n = a.size();
    for (std::size_t k = 0; k < n; ++k) {
        if (std::abs(glm::distance(a[k], a[(k + 1) % n]) - glm::distance(b[k], b[(k + 1) % n])) > 2e-5f) return false;
        if (std::abs(glm::distance(a[0], a[k]) - glm::distance(b[0], b[k])) > 2e-5f) return false;
        if (std::abs(glm::distance(a[1], a[k]) - glm::distance(b[1], b[k])) > 2e-5f) return false;
    }
    return true;
}

/// Renumérote les points d'une pièce : nouveau point k = ancien point (shift ± k). Coutures et pose suivent.
void reindexOutline(Garment& g, int panel, int shift, bool reversed)
{
    auto& pts = g.panels[static_cast<std::size_t>(panel)].points;
    const int n = static_cast<int>(pts.size());
    auto oldAt = [&](int k) { return (((reversed ? shift - k : shift + k) % n) + n) % n; };
    std::vector<int> pos(static_cast<std::size_t>(n));
    std::vector<glm::vec2> next(static_cast<std::size_t>(n));
    for (int k = 0; k < n; ++k) {
        next[static_cast<std::size_t>(k)] = pts[static_cast<std::size_t>(oldAt(k))];
        pos[static_cast<std::size_t>(oldAt(k))] = k;
    }
    auto chainStart = [&](int e, int count) { return reversed ? pos[static_cast<std::size_t>((e + count) % n)] : pos[static_cast<std::size_t>(e)]; };
    for (auto& s : g.seams) {
        if (s.a.panel == panel) s.a.edge = chainStart(s.a.edge, s.countA);
        if (s.b.panel == panel) s.b.edge = chainStart(s.b.edge, s.countB);
    }
    for (auto& a : g.attachments)
        if (a.patch == panel && a.count > 0) a.startEdge = chainStart(a.startEdge, a.count);
    pts = std::move(next);
}

int nextGroup(const Garment& g)
{
    int m = 0;
    for (const auto& p : g.panels) m = std::max(m, p.linkGroup);
    return m + 1;
}

} // namespace

glm::vec2 linkTransform(glm::vec2 v, float angleDeg, bool mirror)
{
    if (mirror) v.x = -v.x;
    const float r = glm::radians(angleDeg), c = std::cos(r), s = std::sin(r);
    return {c * v.x - s * v.y, s * v.x + c * v.y};
}

Garment extractPanels(const Garment& g, const std::vector<int>& idsIn)
{
    const auto ids = validIds(g, idsIn);
    Garment out;
    for (const int p : ids) out.panels.push_back(g.panels[static_cast<std::size_t>(p)]);
    for (const auto& s : g.seams) {
        const int a = indexIn(ids, s.a.panel), b = indexIn(ids, s.b.panel);
        if (a < 0 || b < 0) continue;
        Seam c = s;
        c.a.panel = a;
        c.b.panel = b;
        out.seams.push_back(c);
    }
    for (const auto& at : g.attachments) {
        const int p = indexIn(ids, at.patch), b = indexIn(ids, at.base);
        if (p < 0 || b < 0) continue;
        Attachment c = at;
        c.patch = p;
        c.base = b;
        out.attachments.push_back(c);
    }
    return out;
}

void mirrorGarment(Garment& part, float axis)
{
    for (auto& p : part.panels) {
        for (auto& q : p.points) q.x = 2.0f * axis - q.x;   // même ordre de points : les coutures restent valides
        p.grainDeg = wrapDeg(180.0f - p.grainDeg);
        p.symAxisX = 2.0f * axis - p.symAxisX;
        auto& pl = p.placement;
        pl.angleDeg = wrapDeg(-pl.angleDeg);
        pl.zone = mirroredZone(pl.zone);
        pl.offset.x = -pl.offset.x;
        pl.rotation = glm::quat(pl.rotation.w, pl.rotation.x, -pl.rotation.y, -pl.rotation.z);
        p.linkMirror = !p.linkMirror;
        p.linkAngle = wrapDeg(-p.linkAngle);
        for (auto& pr : p.prints) {   // l'impression change de côté, l'image reste lisible
            pr.offset.x = -pr.offset.x;
            pr.angleDeg = -pr.angleDeg;
        }
        for (auto& f : p.folds) {
            f.a.x = -f.a.x;
            f.b.x = -f.b.x;
        }
    }
    for (auto& a : part.attachments) a.at.x = 2.0f * axis - a.at.x;
}

std::vector<int> appendPanels(Garment& g, const Garment& part, glm::vec2 offset)
{
    const int base = static_cast<int>(g.panels.size());
    std::vector<int> out;
    for (const auto& p : part.panels) {
        Panel c = p;
        for (auto& q : c.points) q += offset;
        c.symAxisX += offset.x;
        g.panels.push_back(c);
        out.push_back(static_cast<int>(g.panels.size()) - 1);
    }
    for (auto s : part.seams) {
        s.a.panel += base;
        s.b.panel += base;
        g.seams.push_back(s);
    }
    for (auto a : part.attachments) {
        a.patch += base;
        a.base += base;
        a.at += offset;
        g.attachments.push_back(a);
    }
    return out;
}

std::vector<int> clonePanels(Garment& g, const std::vector<int>& idsIn, const CloneOptions& o)
{
    const auto ids = validIds(g, idsIn);
    if (ids.empty()) return {};
    if (o.link)
        for (const int p : ids) {
            auto& pn = g.panels[static_cast<std::size_t>(p)];
            if (pn.linkGroup == 0) {
                pn.linkGroup = nextGroup(g);
                pn.linkMirror = false;
                pn.linkAngle = 0.0f;
            }
        }
    Garment part = extractPanels(g, ids);
    glm::vec2 lo, hi;
    bounds(g, ids, lo, hi);
    glm::vec2 offset(0.0f);
    float axis = 0.0f;
    if (o.layer != 0) {
        offset = glm::vec2(0.015f, -0.015f);
        for (auto& p : part.panels) {
            p.placement.radius = std::max(0.02f, p.placement.radius + 0.012f * static_cast<float>(o.layer));
            p.name += o.layer > 0 ? " (dessus)" : " (dessous)";
        }
    } else {
        // Le clone est rangé à droite de toutes les pièces, à la même hauteur.
        std::vector<int> every;
        for (int p = 0; p < static_cast<int>(g.panels.size()); ++p) every.push_back(p);
        glm::vec2 alo, ahi;
        bounds(g, every, alo, ahi);
        if (o.mirror) {
            axis = hi.x + 0.015f;
            mirrorGarment(part, axis);
            offset = glm::vec2(ahi.x - hi.x, 0.0f);
            for (auto& p : part.panels) p.name += " (sym.)";
        } else {
            offset = glm::vec2(ahi.x + 0.03f - lo.x, 0.0f);
            for (auto& p : part.panels) p.name += " (clone)";
        }
    }
    if (!o.link)
        for (auto& p : part.panels) p.linkGroup = 0;
    const std::size_t seamsBefore = g.seams.size(), attachBefore = g.attachments.size();
    const auto clones = appendPanels(g, part, offset);

    if (o.mirror && o.sewing) {
        // Coutures vers une pièce voisine : le clone est cousu sur le côté opposé de cette pièce.
        auto mirroredChain = [&](int q, int edge, int count, int& outEdge) {
            const auto& pn = g.panels[static_cast<std::size_t>(q)];
            const int n = static_cast<int>(pn.points.size());
            if (pn.symmetric) {
                auto pm = [&](int i) {
                    i = ((i % n) + n) % n;
                    const int s = symmetricPartner(pn, i);
                    if (s >= 0) return s;
                    return std::abs(pn.points[static_cast<std::size_t>(i)].x - pn.symAxisX) < 0.0015f ? i : -1;
                };
                std::vector<int> chain;
                for (int k = 0; k <= count; ++k) chain.push_back(pm(edge + k));
                if (std::find(chain.begin(), chain.end(), -1) != chain.end()) return false;
                bool asc = true, desc = true;
                for (int k = 1; k <= count; ++k) {
                    asc = asc && chain[static_cast<std::size_t>(k)] == (chain[static_cast<std::size_t>(k - 1)] + 1) % n;
                    desc = desc && chain[static_cast<std::size_t>(k - 1)] == (chain[static_cast<std::size_t>(k)] + 1) % n;
                }
                if (desc) outEdge = chain.back();
                else if (asc) outEdge = chain.front();
                else return false;
                return outEdge != edge;
            }
            if (pn.linkGroup > 0)
                for (int j = 0; j < static_cast<int>(g.panels.size()); ++j) {
                    const auto& other = g.panels[static_cast<std::size_t>(j)];
                    if (j == q || indexIn(ids, j) >= 0 || indexIn(clones, j) >= 0) continue;
                    if (other.linkGroup == pn.linkGroup && other.linkMirror != pn.linkMirror &&
                        other.points.size() == pn.points.size()) {
                        outEdge = edge;
                        return true;
                    }
                }
            return false;
        };
        auto partnerOf = [&](int q) {   // pièce liée en miroir de q, ou q elle-même si symétrique
            const auto& pn = g.panels[static_cast<std::size_t>(q)];
            if (pn.symmetric) return q;
            for (int j = 0; j < static_cast<int>(g.panels.size()); ++j) {
                const auto& other = g.panels[static_cast<std::size_t>(j)];
                if (j != q && pn.linkGroup > 0 && indexIn(ids, j) < 0 && indexIn(clones, j) < 0 &&
                    other.linkGroup == pn.linkGroup && other.linkMirror != pn.linkMirror && other.points.size() == pn.points.size())
                    return j;
            }
            return -1;
        };
        for (std::size_t si = 0; si < seamsBefore; ++si) {
            const Seam s = g.seams[si];
            const int ia = indexIn(ids, s.a.panel), ib = indexIn(ids, s.b.panel);
            if ((ia >= 0) == (ib >= 0)) continue;
            const bool aInside = ia >= 0;
            const EdgeRef inside = aInside ? s.a : s.b, outside = aInside ? s.b : s.a;
            const int cIn = aInside ? s.countA : s.countB, cOut = aInside ? s.countB : s.countA;
            int edge = -1;
            if (!mirroredChain(outside.panel, outside.edge, cOut, edge)) continue;
            Seam c = s;
            const EdgeRef cloneSide{clones[static_cast<std::size_t>(aInside ? ia : ib)], inside.edge};
            const EdgeRef otherSide{partnerOf(outside.panel), edge};
            if (otherSide.panel < 0) continue;
            c.a = aInside ? cloneSide : otherSide;
            c.b = aInside ? otherSide : cloneSide;
            c.countA = aInside ? cIn : cOut;
            c.countB = aInside ? cOut : cIn;
            g.seams.push_back(c);
        }
        // Poches posées sur une pièce symétrique : le clone est posé en face.
        for (std::size_t ai = 0; ai < attachBefore; ++ai) {
            const Attachment a = g.attachments[ai];
            const int ip = indexIn(ids, a.patch);
            if (ip < 0 || indexIn(ids, a.base) >= 0) continue;
            const auto& base = g.panels[static_cast<std::size_t>(a.base)];
            Attachment c = a;
            c.patch = clones[static_cast<std::size_t>(ip)];
            if (base.symmetric) {
                c.at.x = 2.0f * base.symAxisX - a.at.x;
            } else {
                const int partner = partnerOf(a.base);
                if (partner < 0) continue;
                c.base = partner;
                const glm::vec2 cb = centroid(base.points), cp = centroid(g.panels[static_cast<std::size_t>(partner)].points);
                const auto& pb = g.panels[static_cast<std::size_t>(partner)];
                c.at = cp + linkTransform(inverseLink(a.at - cb, base.linkAngle, base.linkMirror), pb.linkAngle, pb.linkMirror);
            }
            g.attachments.push_back(c);
        }
    }
    return clones;
}

void rotatePanels(Garment& g, const std::vector<int>& idsIn, float degrees)
{
    const auto ids = validIds(g, idsIn);
    if (ids.empty()) return;
    glm::vec2 lo, hi;
    bounds(g, ids, lo, hi);
    const glm::vec2 c = (lo + hi) * 0.5f;
    for (const int p : ids) {
        auto& pn = g.panels[static_cast<std::size_t>(p)];
        std::vector<glm::vec2> printAt;
        for (const auto& pr : pn.prints) printAt.push_back(c + linkTransform(printCenter(pn, pr) - c, degrees, false));
        std::vector<glm::vec2> foldAt;
        const glm::vec2 c0 = panelBoxCenter(pn);
        for (const auto& f : pn.folds)
            for (const glm::vec2 q : {f.a, f.b}) foldAt.push_back(c + linkTransform(c0 + q - c, degrees, false));
        for (auto& q : pn.points) q = c + linkTransform(q - c, degrees, false);
        for (std::size_t i = 0; i < pn.folds.size(); ++i) {
            pn.folds[i].a = foldAt[2 * i] - panelBoxCenter(pn);
            pn.folds[i].b = foldAt[2 * i + 1] - panelBoxCenter(pn);
        }
        for (std::size_t i = 0; i < pn.prints.size(); ++i) {
            pn.prints[i].offset = printAt[i] - panelBoxCenter(pn);
            pn.prints[i].angleDeg = wrapDeg(pn.prints[i].angleDeg + degrees);
        }
        pn.grainDeg = wrapDeg(pn.grainDeg + degrees);
        pn.linkAngle = wrapDeg(pn.linkAngle + degrees);
        if (pn.symmetric) {
            const float turns = degrees / 180.0f;
            if (std::abs(turns - std::round(turns)) > 1e-4f) pn.symmetric = false;   // l'axe n'est plus vertical
            else pn.symAxisX = (c + linkTransform(glm::vec2(pn.symAxisX, c.y) - c, degrees, false)).x;
        }
    }
    for (auto& a : g.attachments)
        if (indexIn(ids, a.base) >= 0) a.at = c + linkTransform(a.at - c, degrees, false);
}

void flipPanels(Garment& g, const std::vector<int>& idsIn, bool horizontal)
{
    const auto ids = validIds(g, idsIn);
    if (ids.empty()) return;
    glm::vec2 lo, hi;
    bounds(g, ids, lo, hi);
    const glm::vec2 c = (lo + hi) * 0.5f;
    for (const int p : ids) {
        auto& pn = g.panels[static_cast<std::size_t>(p)];
        for (auto& q : pn.points) {
            if (horizontal) q.x = 2.0f * c.x - q.x;
            else q.y = 2.0f * c.y - q.y;
        }
        pn.grainDeg = wrapDeg(horizontal ? 180.0f - pn.grainDeg : -pn.grainDeg);
        pn.linkMirror = !pn.linkMirror;
        pn.linkAngle = wrapDeg(horizontal ? -pn.linkAngle : 180.0f - pn.linkAngle);
        if (horizontal) pn.symAxisX = 2.0f * c.x - pn.symAxisX;
        for (auto& pr : pn.prints) {
            if (horizontal) pr.offset.x = -pr.offset.x;
            else pr.offset.y = -pr.offset.y;
            pr.angleDeg = -pr.angleDeg;
        }
        for (auto& f : pn.folds)
            for (glm::vec2* q : {&f.a, &f.b}) {
                if (horizontal) q->x = -q->x;
                else q->y = -q->y;
            }
    }
    for (auto& a : g.attachments)
        if (indexIn(ids, a.base) >= 0) {
            if (horizontal) a.at.x = 2.0f * c.x - a.at.x;
            else a.at.y = 2.0f * c.y - a.at.y;
        }
}

std::vector<int> reorderPanels(Garment& g, const std::vector<int>& idsIn, int move)
{
    const auto ids = validIds(g, idsIn);
    const int n = static_cast<int>(g.panels.size());
    std::vector<int> order(static_cast<std::size_t>(n));
    for (int k = 0; k < n; ++k) order[static_cast<std::size_t>(k)] = k;
    auto chosen = [&](int p) { return indexIn(ids, p) >= 0; };
    if (move == 2 || move == -2) {
        std::stable_partition(order.begin(), order.end(), [&](int p) { return move == 2 ? !chosen(p) : chosen(p); });
    } else if (move == 1) {
        for (int k = n - 2; k >= 0; --k)
            if (chosen(order[static_cast<std::size_t>(k)]) && !chosen(order[static_cast<std::size_t>(k + 1)]))
                std::swap(order[static_cast<std::size_t>(k)], order[static_cast<std::size_t>(k + 1)]);
    } else if (move == -1) {
        for (int k = 1; k < n; ++k)
            if (chosen(order[static_cast<std::size_t>(k)]) && !chosen(order[static_cast<std::size_t>(k - 1)]))
                std::swap(order[static_cast<std::size_t>(k)], order[static_cast<std::size_t>(k - 1)]);
    }
    permute(g, order);
    std::vector<int> out;
    for (const int p : ids)
        out.push_back(static_cast<int>(std::find(order.begin(), order.end(), p) - order.begin()));
    return out;
}

std::vector<glm::vec2> offsetOutline(const std::vector<glm::vec2>& pts, float d)
{
    const std::size_t n = pts.size();
    if (n < 3) return pts;
    const float sign = signedArea(pts) > 0.0f ? 1.0f : -1.0f;   // sens trigonométrique : extérieur à droite des bords
    std::vector<glm::vec2> out(n);
    for (std::size_t i = 0; i < n; ++i) {
        const glm::vec2 a = pts[(i + n - 1) % n], b = pts[i], c = pts[(i + 1) % n];
        auto normal = [&](glm::vec2 u, glm::vec2 v) {
            const glm::vec2 e = v - u;
            const float l = glm::length(e);
            return l > 1e-9f ? glm::vec2(e.y, -e.x) / l * sign : glm::vec2(0.0f);
        };
        const glm::vec2 n1 = normal(a, b), n2 = normal(b, c);
        glm::vec2 m = n1 + n2;
        const float denom = 1.0f + glm::dot(n1, n2);
        if (denom < 0.15f) m = glm::length(m) > 1e-6f ? glm::normalize(m) * 3.0f : n1;   // pointe très aiguë : biseau limité
        else m /= denom;
        out[i] = b + m * d;
    }
    return out;
}

bool offsetPanel(Garment& g, int panel, float d)
{
    if (panel < 0 || panel >= static_cast<int>(g.panels.size())) return false;
    auto& pn = g.panels[static_cast<std::size_t>(panel)];
    const auto pts = offsetOutline(pn.points, d);
    if (d < 0.0f && std::abs(signedArea(pts)) < 1e-6f) return false;
    if ((signedArea(pts) > 0.0f) != (signedArea(pn.points) > 0.0f)) return false;   // la pièce s'est retournée
    std::vector<int> hint(pn.points.size());
    for (std::size_t k = 0; k < hint.size(); ++k) hint[k] = static_cast<int>(k);
    replaceOutline(g, panel, pts, &hint);
    return true;
}

int simplifyPanel(Garment& g, int panel, float tolerance)
{
    if (panel < 0 || panel >= static_cast<int>(g.panels.size())) return 0;
    const auto& pn = g.panels[static_cast<std::size_t>(panel)];
    const int n = static_cast<int>(pn.points.size());
    std::vector<char> keep(static_cast<std::size_t>(n), 0);
    for (const auto& s : g.seams)
        for (int side = 0; side < 2; ++side)
            for (const auto& e : g.seamEdges(s, side))
                if (e.panel == panel) {
                    keep[static_cast<std::size_t>(e.edge)] = 1;
                    keep[static_cast<std::size_t>((e.edge + 1) % n)] = 1;
                }
    if (const int ai = g.attachmentOf(panel); ai >= 0 && g.attachments[static_cast<std::size_t>(ai)].count > 0) return 0;
    std::vector<int> idx;
    for (int k = 0; k < n; ++k) idx.push_back(k);
    int removed = 0;
    while (idx.size() > 3) {
        int best = -1;
        float bestD = tolerance;
        const std::size_t m = idx.size();
        for (std::size_t k = 0; k < m; ++k) {
            if (keep[static_cast<std::size_t>(idx[k])]) continue;
            const glm::vec2 a = pn.points[static_cast<std::size_t>(idx[(k + m - 1) % m])], b = pn.points[static_cast<std::size_t>(idx[k])],
                            c = pn.points[static_cast<std::size_t>(idx[(k + 1) % m])];
            const float dist = distanceToSegment(b, a, c);
            if (dist < bestD) {
                bestD = dist;
                best = static_cast<int>(k);
            }
        }
        if (best < 0) break;
        idx.erase(idx.begin() + best);
        ++removed;
    }
    if (removed == 0) return 0;
    std::vector<glm::vec2> pts;
    for (const int k : idx) pts.push_back(pn.points[static_cast<std::size_t>(k)]);
    const bool symmetric = pn.symmetric;
    replaceOutline(g, panel, pts);
    if (symmetric) symmetrizePanel(g, panel, true);
    return removed;
}

int addInnerShape(Garment& g, int panel, float inset)
{
    if (panel < 0 || panel >= static_cast<int>(g.panels.size()) || g.attachmentOf(panel) >= 0) return -1;
    const Panel& base = g.panels[static_cast<std::size_t>(panel)];
    const auto pts = offsetOutline(base.points, -inset);
    if ((signedArea(pts) > 0.0f) != (signedArea(base.points) > 0.0f) || std::abs(signedArea(pts)) < 1e-5f) return -1;
    glm::vec2 lo(1e9f), hi(-1e9f), blo(1e9f), bhi(-1e9f);
    for (const auto& q : pts) {
        lo = glm::min(lo, q);
        hi = glm::max(hi, q);
    }
    for (const auto& q : base.points) {
        blo = glm::min(blo, q);
        bhi = glm::max(bhi, q);
    }
    Panel inner;
    inner.name = "Renfort " + base.name;
    inner.color = base.color * 0.8f;
    inner.grainDeg = base.grainDeg;
    inner.placement = base.placement;
    // Dessiné à côté de la pièce dans le patron ; posé dessus en 3D.
    const glm::vec2 shift(bhi.x - lo.x + 0.04f, 0.0f);
    for (const auto& q : pts) inner.points.push_back(q + shift);
    g.panels.push_back(inner);
    const int idx = static_cast<int>(g.panels.size()) - 1;
    if (g.attachPatch(idx, panel, (lo + hi) * 0.5f, AttachKind::AllRound) < 0) {
        g.panels.pop_back();
        return -1;
    }
    return idx;
}

bool slashAndSpread(Garment& g, int panel, int iA, int iB, float degrees)
{
    if (panel < 0 || panel >= static_cast<int>(g.panels.size())) return false;
    const auto pts = g.panels[static_cast<std::size_t>(panel)].points;
    const int n = static_cast<int>(pts.size());
    if (iA < 0 || iB < 0 || iA >= n || iB >= n || iA == iB || std::abs(degrees) < 1e-3f) return false;
    const int span = ((iB - iA) % n + n) % n;   // nombre de pas de A à B
    if (span < 2) return false;                 // rien entre A et B : rien à faire pivoter
    auto inSide = [&](int k) { return k != iA && k != iB && ((k - iA) % n + n) % n < span; };
    const glm::vec2 pivot = pts[static_cast<std::size_t>(iB)];
    std::vector<glm::vec2> best;
    float bestArea = -1.0f;
    for (const float sign : {1.0f, -1.0f}) {
        std::vector<glm::vec2> out;
        for (int k = 0; k < n; ++k) {
            const glm::vec2 q = pts[static_cast<std::size_t>(k)];
            out.push_back(inSide(k) ? pivot + linkTransform(q - pivot, sign * degrees, false) : q);
            if (k == iA) out.push_back(pivot + linkTransform(q - pivot, sign * degrees, false));   // bord de l'ouverture
        }
        const float area = std::abs(signedArea(out));
        if (area > bestArea) {
            bestArea = area;
            best = std::move(out);
        }
    }
    std::vector<int> hint(static_cast<std::size_t>(n));
    for (int k = 0; k < n; ++k) hint[static_cast<std::size_t>(k)] = k + (k > iA ? 1 : 0);
    replaceOutline(g, panel, best, &hint);
    return true;
}

namespace {
// Longueur maximale de tangente au coin k : on garde un peu de chaque bord.
float cornerLimit(const std::vector<glm::vec2>& pts, int k)
{
    const int n = static_cast<int>(pts.size());
    const glm::vec2 c = pts[static_cast<std::size_t>(k)];
    const float la = glm::distance(c, pts[static_cast<std::size_t>((k + n - 1) % n)]);
    const float lb = glm::distance(c, pts[static_cast<std::size_t>((k + 1) % n)]);
    return 0.98f * std::min(la, lb);
}
} // namespace

float maxCornerTangent(const Garment& g, int panel, int point)
{
    if (panel < 0 || panel >= static_cast<int>(g.panels.size())) return 0.0f;
    const auto& pts = g.panels[static_cast<std::size_t>(panel)].points;
    if (point < 0 || point >= static_cast<int>(pts.size()) || pts.size() < 3) return 0.0f;
    return cornerLimit(pts, point);
}

int roundCorner(Garment& g, int panel, int point, float tangentLength, int segments)
{
    if (panel < 0 || panel >= static_cast<int>(g.panels.size())) return -1;
    const auto pts = g.panels[static_cast<std::size_t>(panel)].points;
    const int n = static_cast<int>(pts.size());
    if (point < 0 || point >= n || n < 3 || segments < 2) return -1;
    const glm::vec2 c = pts[static_cast<std::size_t>(point)];
    const glm::vec2 a = pts[static_cast<std::size_t>((point + n - 1) % n)], b = pts[static_cast<std::size_t>((point + 1) % n)];
    const float t = std::min(tangentLength, cornerLimit(pts, point));
    if (t < 1e-4f) return -1;
    const glm::vec2 ua = glm::normalize(a - c), ub = glm::normalize(b - c);
    const float cosT = glm::clamp(glm::dot(ua, ub), -1.0f, 1.0f);
    if (cosT < -0.999f) return -1;   // bords alignés : pas de coin à arrondir
    const glm::vec2 p1 = c + ua * t, p2 = c + ub * t;
    const float half = std::acos(cosT) * 0.5f;   // demi-angle du coin
    const glm::vec2 bis = glm::normalize(ua + ub);
    const glm::vec2 center = c + bis * (t / std::cos(half));
    const glm::vec2 r1 = p1 - center, r2 = p2 - center;
    float a1 = std::atan2(r1.y, r1.x), a2 = std::atan2(r2.y, r2.x);
    float sweep = a2 - a1;
    while (sweep > glm::pi<float>()) sweep -= 2.0f * glm::pi<float>();
    while (sweep < -glm::pi<float>()) sweep += 2.0f * glm::pi<float>();
    const float radius = glm::length(r1);
    std::vector<glm::vec2> out;
    out.reserve(static_cast<std::size_t>(n + segments));
    for (int k = 0; k < n; ++k) {
        if (k != point) {
            out.push_back(pts[static_cast<std::size_t>(k)]);
            continue;
        }
        for (int s = 0; s <= segments; ++s) {
            const float ang = a1 + sweep * static_cast<float>(s) / static_cast<float>(segments);
            out.push_back(center + radius * glm::vec2(std::cos(ang), std::sin(ang)));
        }
    }
    // Un point de l'arc qui retombe sur le voisin (tangente = longueur du bord) : on l'enlève.
    std::vector<glm::vec2> clean;
    for (const auto& q : out)
        if (clean.empty() || glm::distance(clean.back(), q) > 1e-4f) clean.push_back(q);
    while (clean.size() > 3 && glm::distance(clean.front(), clean.back()) < 1e-4f) clean.pop_back();
    replaceOutline(g, panel, clean);
    const auto& now = g.panels[static_cast<std::size_t>(panel)].points;
    for (int k = 0; k < static_cast<int>(now.size()); ++k)
        if (glm::distance(now[static_cast<std::size_t>(k)], p1) < 1e-4f) return k;
    return 0;
}

void curveAround(const Garment& g, int panel, int edge, int& first, int& count, float maxTurnDeg)
{
    first = edge;
    count = 1;
    if (panel < 0 || panel >= static_cast<int>(g.panels.size())) return;
    const auto& pts = g.panels[static_cast<std::size_t>(panel)].points;
    const int n = static_cast<int>(pts.size());
    if (edge < 0 || edge >= n) return;
    const float cosMax = std::cos(glm::radians(maxTurnDeg));
    auto smoothAt = [&](int k) {   // le contour tourne peu au point k
        const glm::vec2 d0 = pts[static_cast<std::size_t>(k)] - pts[static_cast<std::size_t>((k + n - 1) % n)];
        const glm::vec2 d1 = pts[static_cast<std::size_t>((k + 1) % n)] - pts[static_cast<std::size_t>(k)];
        const float l0 = glm::length(d0), l1 = glm::length(d1);
        if (l0 < 1e-6f || l1 < 1e-6f) return false;
        // Un bord beaucoup plus long que son voisin (bord droit qui rejoint une courbe) : fin de la courbe.
        if (std::max(l0, l1) > 1.6f * std::min(l0, l1)) return false;
        return glm::dot(d0, d1) / (l0 * l1) > cosMax;
    };
    // Bords droits : pas de courbe à suivre (le bord seul est coupé).
    auto bent = [&](int k) {
        const glm::vec2 d0 = pts[static_cast<std::size_t>(k)] - pts[static_cast<std::size_t>((k + n - 1) % n)];
        const glm::vec2 d1 = pts[static_cast<std::size_t>((k + 1) % n)] - pts[static_cast<std::size_t>(k)];
        const float l0 = glm::length(d0), l1 = glm::length(d1);
        return l0 > 1e-6f && l1 > 1e-6f && glm::dot(d0, d1) / (l0 * l1) < 0.99985f;   // > 1° environ
    };
    while (count < n - 1 && smoothAt(first) && bent(first)) {
        first = (first + n - 1) % n;
        ++count;
    }
    int end = (edge + 1) % n;
    while (count < n - 1 && smoothAt(end) && bent(end)) {
        end = (end + 1) % n;
        ++count;
    }
}

bool splitCurve(Garment& g, int panel, int edge, int parts, std::vector<glm::vec2>* cuts)
{
    if (panel < 0 || panel >= static_cast<int>(g.panels.size()) || parts < 1) return false;
    const auto pts = g.panels[static_cast<std::size_t>(panel)].points;
    const int n = static_cast<int>(pts.size());
    if (edge < 0 || edge >= n) return false;
    int first = edge, count = 1;
    curveAround(g, panel, edge, first, count);
    // Longueurs cumulées le long de la courbe
    std::vector<float> acc{0.0f};
    for (int k = 0; k < count; ++k)
        acc.push_back(acc.back() + glm::distance(pts[static_cast<std::size_t>((first + k) % n)], pts[static_cast<std::size_t>((first + k + 1) % n)]));
    const float total = acc.back();
    if (total < 1e-5f) return false;
    // Points de coupe : sur un point existant, on le garde ; sinon on l'ajoute dans son bord.
    // La forme de la courbe ne change pas.
    std::vector<std::vector<glm::vec2>> extra(static_cast<std::size_t>(count));
    for (int i = 1; i < parts; ++i) {
        const float want = total * static_cast<float>(i) / static_cast<float>(parts);
        int seg = 0;
        while (seg + 1 < count && acc[static_cast<std::size_t>(seg + 1)] < want) ++seg;
        const glm::vec2 a = pts[static_cast<std::size_t>((first + seg) % n)], b = pts[static_cast<std::size_t>((first + seg + 1) % n)];
        const float len = acc[static_cast<std::size_t>(seg + 1)] - acc[static_cast<std::size_t>(seg)];
        const glm::vec2 q = glm::mix(a, b, len > 1e-7f ? (want - acc[static_cast<std::size_t>(seg)]) / len : 0.0f);
        if (cuts) cuts->push_back(q);
        if (glm::distance(q, a) > 1e-4f && glm::distance(q, b) > 1e-4f) extra[static_cast<std::size_t>(seg)].push_back(q);
    }
    std::vector<glm::vec2> out;
    bool added = false;
    for (int k = 0; k < n; ++k) {
        out.push_back(pts[static_cast<std::size_t>(k)]);
        const int rel = ((k - first) % n + n) % n;
        if (rel < count)
            for (const auto& q : extra[static_cast<std::size_t>(rel)]) {
                out.push_back(q);
                added = true;
            }
    }
    if (added) replaceOutline(g, panel, out);
    return added;
}

int addDart(Garment& g, int panel, int point, float depth, float width)
{
    if (panel < 0 || panel >= static_cast<int>(g.panels.size())) return -1;
    const auto pts = g.panels[static_cast<std::size_t>(panel)].points;
    const int n = static_cast<int>(pts.size());
    if (point < 0 || point >= n || n < 3 || depth <= 0.0f || width <= 0.0f) return -1;
    const glm::vec2 c = pts[static_cast<std::size_t>(point)];
    const glm::vec2 a = pts[static_cast<std::size_t>((point + n - 1) % n)], b = pts[static_cast<std::size_t>((point + 1) % n)];
    const float half = std::min(width * 0.5f, 0.45f * std::min(glm::distance(a, c), glm::distance(b, c)));
    if (half < 1e-4f) return -1;
    const glm::vec2 pa = c + glm::normalize(a - c) * half, pb = c + glm::normalize(b - c) * half;
    glm::vec2 tan = pb - pa;
    if (glm::length(tan) < 1e-6f) return -1;
    tan = glm::normalize(tan);
    // Intérieur : à gauche du sens de parcours si le contour tourne dans le sens trigonométrique.
    glm::vec2 inward(-tan.y, tan.x);
    if (signedArea(pts) < 0.0f) inward = -inward;
    const glm::vec2 tip = c + inward * depth;
    std::vector<glm::vec2> out;
    for (int k = 0; k < n; ++k) {
        if (k != point) {
            out.push_back(pts[static_cast<std::size_t>(k)]);
            continue;
        }
        out.push_back(pa);
        out.push_back(tip);
        out.push_back(pb);
    }
    replaceOutline(g, panel, out);
    const auto& now = g.panels[static_cast<std::size_t>(panel)].points;
    for (int k = 0; k < static_cast<int>(now.size()); ++k)
        if (glm::distance(now[static_cast<std::size_t>(k)], tip) < 1e-5f) return k;
    return -1;
}

bool alignPoint(Garment& g, int panel, int point, bool horizontal, bool next)
{
    if (panel < 0 || panel >= static_cast<int>(g.panels.size())) return false;
    auto& pts = g.panels[static_cast<std::size_t>(panel)].points;
    const int n = static_cast<int>(pts.size());
    if (point < 0 || point >= n || n < 3) return false;
    const glm::vec2 ref = pts[static_cast<std::size_t>(next ? (point + 1) % n : (point + n - 1) % n)];
    auto& q = pts[static_cast<std::size_t>(point)];
    if (horizontal)
        q.y = ref.y;
    else
        q.x = ref.x;
    return true;
}

bool unfoldPanel(Garment& g, int panel, int edge)
{
    if (panel < 0 || panel >= static_cast<int>(g.panels.size())) return false;
    auto& pn = g.panels[static_cast<std::size_t>(panel)];
    const auto pts = pn.points;
    const int n = static_cast<int>(pts.size());
    if (edge < 0 || edge >= n || n < 3) return false;
    const glm::vec2 p0 = pts[static_cast<std::size_t>(edge)], p1 = pts[static_cast<std::size_t>((edge + 1) % n)];
    const glm::vec2 d = p1 - p0;
    if (glm::length(d) < 1e-5f) return false;
    const glm::vec2 u = glm::normalize(d);
    auto mirror = [&](glm::vec2 q) {
        const glm::vec2 r = q - p0;
        return p0 + 2.0f * glm::dot(r, u) * u - r;
    };
    // Contour : de p1 à p0 par le côté de la pièce, puis le même chemin en miroir, de p0 à p1.
    std::vector<glm::vec2> out;
    for (int k = 0; k < n; ++k) out.push_back(pts[static_cast<std::size_t>((edge + 1 + k) % n)]);
    for (int k = n - 2; k >= 1; --k) out.push_back(mirror(pts[static_cast<std::size_t>((edge + 1 + k) % n)]));
    // Points du côté miroir qui retombent sur le pli (déjà présents) : retirés.
    std::vector<glm::vec2> clean;
    for (const auto& q : out)
        if (clean.empty() || glm::distance(clean.back(), q) > 1e-5f) clean.push_back(q);
    if (clean.size() < 3 || std::abs(signedArea(clean)) <= std::abs(signedArea(pts)) * 1.01f) return false;
    // Coutures posées sur le pli : elles disparaissent (le pli n'est plus un bord).
    std::erase_if(g.seams, [&](const Seam& s) {
        auto onFold = [&](const EdgeRef& e, int cnt) {
            if (e.panel != panel) return false;
            for (int k = 0; k < cnt; ++k)
                if ((e.edge + k) % n == edge) return true;
            return false;
        };
        return onFold(s.a, s.countA) || onFold(s.b, s.countB);
    });
    replaceOutline(g, panel, clean);
    if (std::abs(u.x) < 1e-3f) {   // pli vertical : pièce symétrique autour de lui
        g.panels[static_cast<std::size_t>(panel)].symmetric = true;
        g.panels[static_cast<std::size_t>(panel)].symAxisX = p0.x;
    }
    return true;
}

float contourLength(const Garment& g, int panel)
{
    if (panel < 0 || panel >= static_cast<int>(g.panels.size())) return 0.0f;
    const auto& pts = g.panels[static_cast<std::size_t>(panel)].points;
    float len = 0.0f;
    for (std::size_t k = 0; k < pts.size(); ++k) len += glm::distance(pts[k], pts[(k + 1) % pts.size()]);
    return len;
}

float contourParam(const Garment& g, int panel, int edge, float t)
{
    if (panel < 0 || panel >= static_cast<int>(g.panels.size())) return 0.0f;
    const auto& pts = g.panels[static_cast<std::size_t>(panel)].points;
    const int n = static_cast<int>(pts.size());
    float s = 0.0f;
    for (int k = 0; k < edge && k < n; ++k) s += glm::distance(pts[static_cast<std::size_t>(k)], pts[static_cast<std::size_t>((k + 1) % n)]);
    if (edge >= 0 && edge < n) s += glm::clamp(t, 0.0f, 1.0f) * glm::distance(pts[static_cast<std::size_t>(edge)], pts[static_cast<std::size_t>((edge + 1) % n)]);
    return s;
}

glm::vec2 contourPoint(const Garment& g, int panel, float s)
{
    if (panel < 0 || panel >= static_cast<int>(g.panels.size())) return glm::vec2(0.0f);
    const auto& pts = g.panels[static_cast<std::size_t>(panel)].points;
    const int n = static_cast<int>(pts.size());
    const float total = contourLength(g, panel);
    if (n == 0 || total <= 0.0f) return n ? pts[0] : glm::vec2(0.0f);
    s = std::fmod(s, total);
    if (s < 0.0f) s += total;
    for (int k = 0; k < n; ++k) {
        const glm::vec2 a = pts[static_cast<std::size_t>(k)], b = pts[static_cast<std::size_t>((k + 1) % n)];
        const float len = glm::distance(a, b);
        if (s <= len || k == n - 1) return len > 1e-9f ? glm::mix(a, b, glm::clamp(s / len, 0.0f, 1.0f)) : a;
        s -= len;
    }
    return pts[0];
}

namespace {
// Index du point du contour à la position q ; un point est ajouté sur le bord qui la contient si besoin.
int pointAt(Garment& g, int panel, glm::vec2 q)
{
    auto& pts = g.panels[static_cast<std::size_t>(panel)].points;
    const int n = static_cast<int>(pts.size());
    int best = -1;
    float bd = 1e9f;
    for (int k = 0; k < n; ++k)
        if (const float d = glm::distance(pts[static_cast<std::size_t>(k)], q); d < bd) {
            bd = d;
            best = k;
        }
    if (bd < 2e-4f) return best;   // déjà un point ici (à 0,2 mm près)
    int edge = -1;
    float ed = 1e9f;
    for (int k = 0; k < n; ++k)
        if (const float d = distanceToSegment(q, pts[static_cast<std::size_t>(k)], pts[static_cast<std::size_t>((k + 1) % n)]); d < ed) {
            ed = d;
            edge = k;
        }
    if (edge < 0) return -1;
    g.insertPoint(panel, edge, q);
    return edge + 1;
}
} // namespace

int sewFree(Garment& g, int panelA, float sA, float lenA, int panelB, float sB, float lenB)
{
    const int np = static_cast<int>(g.panels.size());
    if (panelA < 0 || panelB < 0 || panelA >= np || panelB >= np) return -1;
    const float LA = contourLength(g, panelA), LB = contourLength(g, panelB);
    if (std::abs(lenA) < 1e-3f || std::abs(lenB) < 1e-3f || LA <= 0.0f || LB <= 0.0f) return -1;
    lenA = glm::clamp(lenA, -LA, LA);
    lenB = glm::clamp(lenB, -LB, LB);
    // Extrémités en coordonnées du patron (elles ne bougent pas quand on ajoute des points).
    const glm::vec2 a0 = contourPoint(g, panelA, sA), a1 = contourPoint(g, panelA, sA + lenA);
    const glm::vec2 b0 = contourPoint(g, panelB, sB), b1 = contourPoint(g, panelB, sB + lenB);
    const bool fullA = std::abs(lenA) > LA - 1e-4f, fullB = std::abs(lenB) > LB - 1e-4f;
    if (pointAt(g, panelA, a0) < 0 || pointAt(g, panelA, a1) < 0 || pointAt(g, panelB, b0) < 0 || pointAt(g, panelB, b1) < 0) return -1;
    // Index (relus après tous les ajouts : une même pièce peut porter les deux côtés)
    auto idx = [&](int panel, glm::vec2 q) {
        const auto& pts = g.panels[static_cast<std::size_t>(panel)].points;
        for (int k = 0; k < static_cast<int>(pts.size()); ++k)
            if (glm::distance(pts[static_cast<std::size_t>(k)], q) < 2e-4f) return k;
        return -1;
    };
    auto run = [&](int panel, glm::vec2 p0, glm::vec2 p1, float len, bool full, EdgeRef& first, int& count) {
        const int n = static_cast<int>(g.panels[static_cast<std::size_t>(panel)].points.size());
        const int i0 = idx(panel, p0), i1 = idx(panel, p1);
        if (i0 < 0 || i1 < 0) return false;
        if (len > 0.0f) {
            first = {panel, i0};
            count = ((i1 - i0) % n + n) % n;
        } else {
            first = {panel, i1};
            count = ((i0 - i1) % n + n) % n;
        }
        if (count == 0 && full) count = n;
        return count > 0;
    };
    Seam s;
    if (!run(panelA, a0, a1, lenA, fullA, s.a, s.countA) || !run(panelB, b0, b1, lenB, fullB, s.b, s.countB)) return -1;
    // Sur une même pièce, les deux portions ne doivent pas se recouvrir.
    if (panelA == panelB) {
        const int n = static_cast<int>(g.panels[static_cast<std::size_t>(panelA)].points.size());
        for (int i = 0; i < s.countA; ++i)
            for (int j = 0; j < s.countB; ++j)
                if ((s.a.edge + i) % n == (s.b.edge + j) % n) return -1;
    }
    // Les listes de sommets suivent le sens des index : début de la liste = début de la portion si
    // elle avance, sa fin sinon.
    s.order = (lenA > 0.0f) == (lenB > 0.0f) ? 1 : 2;
    for (int side = 0; side < 2; ++side) {
        const EdgeRef st = side == 0 ? s.a : s.b;
        const int cnt = side == 0 ? s.countA : s.countB;
        const int n = static_cast<int>(g.panels[static_cast<std::size_t>(st.panel)].points.size());
        for (int k = 0; k < cnt; ++k) g.removeSeamsOn({st.panel, (st.edge + k) % n});
    }
    g.seams.push_back(s);
    return static_cast<int>(g.seams.size()) - 1;
}

std::vector<int> panelsLike(const Garment& g, int panel, SameProperty prop)
{
    std::vector<int> out;
    if (panel < 0 || panel >= static_cast<int>(g.panels.size())) return out;
    const Panel& ref = g.panels[static_cast<std::size_t>(panel)];
    for (int p = 0; p < static_cast<int>(g.panels.size()); ++p) {
        const Panel& q = g.panels[static_cast<std::size_t>(p)];
        bool same = false;
        switch (prop) {
        case SameProperty::Color: same = glm::distance(q.color, ref.color) < 0.02f; break;
        case SameProperty::Zone: same = q.placement.zone == ref.placement.zone; break;
        case SameProperty::LinkGroup: same = p == panel || (ref.linkGroup > 0 && q.linkGroup == ref.linkGroup); break;
        }
        if (same) out.push_back(p);
    }
    return out;
}

int linkPanels(Garment& g, const std::vector<int>& idsIn)
{
    const auto ids = validIds(g, idsIn);
    if (ids.size() < 2) return 0;
    Panel& ref = g.panels[static_cast<std::size_t>(ids.front())];
    const glm::vec2 cr = centroid(ref.points);
    const float tol = 2e-4f;
    int linked = 0;
    const int group = ref.linkGroup > 0 ? ref.linkGroup : nextGroup(g);
    for (std::size_t k = 1; k < ids.size(); ++k) {
        Panel& pn = g.panels[static_cast<std::size_t>(ids[k])];
        if (pn.points.size() != ref.points.size()) continue;
        const glm::vec2 cp = centroid(pn.points);
        bool found = false;
        const int n = static_cast<int>(pn.points.size());
        for (int mirror = 0; mirror < 2 && !found; ++mirror)
            for (int quarter = 0; quarter < 4 && !found; ++quarter)
                for (int dir = 0; dir < 2 && !found; ++dir)
                    for (int shift = 0; shift < n && !found; ++shift) {
                const float rel = 90.0f * static_cast<float>(quarter);
                bool ok = true;
                for (int i = 0; i < n && ok; ++i) {
                    const int j = (((dir != 0 ? shift - i : shift + i) % n) + n) % n;
                    ok = glm::distance(cp + linkTransform(ref.points[static_cast<std::size_t>(i)] - cr, rel, mirror != 0),
                                       pn.points[static_cast<std::size_t>(j)]) < tol;
                }
                if (!ok) continue;
                if (shift != 0 || dir != 0) reindexOutline(g, ids[k], shift, dir != 0);   // mêmes numéros de points que la référence
                // pn = R(rel) S^mirror (ref) ; ref = R(aR) S^mR (forme) => pn = R(rel) S^m R(aR) S^mR
                const bool m = mirror != 0;
                pn.linkMirror = m != ref.linkMirror;
                pn.linkAngle = wrapDeg(rel + (m ? -ref.linkAngle : ref.linkAngle));
                pn.linkGroup = group;
                found = true;
            }
        if (found) ++linked;
    }
    if (linked > 0 && ref.linkGroup == 0) {
        ref.linkGroup = group;
        ref.linkMirror = false;
        ref.linkAngle = 0.0f;
    }
    return linked;
}

void unlinkPanels(Garment& g, const std::vector<int>& ids)
{
    for (const int p : validIds(g, ids)) {
        auto& pn = g.panels[static_cast<std::size_t>(p)];
        pn.linkGroup = 0;
        pn.linkMirror = false;
        pn.linkAngle = 0.0f;
    }
}

int propagateFrom(Garment& g, int panel, const std::vector<glm::vec2>& oldPoints)
{
    if (panel < 0 || panel >= static_cast<int>(g.panels.size()) || oldPoints.size() < 3) return 0;
    const Panel src = g.panels[static_cast<std::size_t>(panel)];
    if (src.linkGroup <= 0) return 0;
    const glm::vec2 cs = centroid(oldPoints);
    // Ancien point de la source -> son index dans le nouveau contour.
    std::vector<int> srcMap(oldPoints.size(), -1);
    for (std::size_t k = 0; k < oldPoints.size(); ++k) {
        for (std::size_t j = 0; j < src.points.size(); ++j)
            if (glm::distance(src.points[j], oldPoints[k]) < 1e-6f) {
                srcMap[k] = static_cast<int>(j);
                break;
            }
        if (srcMap[k] < 0 && src.points.size() == oldPoints.size()) srcMap[k] = static_cast<int>(k);
    }
    int updated = 0;
    for (int j = 0; j < static_cast<int>(g.panels.size()); ++j) {
        const Panel& t = g.panels[static_cast<std::size_t>(j)];
        if (j == panel || t.linkGroup != src.linkGroup || t.points.size() != oldPoints.size()) continue;
        auto map = [&](glm::vec2 v) { return linkTransform(inverseLink(v, src.linkAngle, src.linkMirror), t.linkAngle, t.linkMirror); };
        const glm::vec2 ct = centroid(t.points);
        bool matches = true;   // le clone correspond-il point à point à l'ancienne source ?
        for (std::size_t k = 0; k < oldPoints.size() && matches; ++k)
            matches = glm::distance(ct + map(oldPoints[k] - cs), t.points[k]) < 5e-4f;
        if (!matches) continue;
        std::vector<glm::vec2> pts;
        for (const auto& q : src.points) pts.push_back(ct + map(q - cs));
        replaceOutline(g, j, pts, &srcMap);
        ++updated;
    }
    return updated;
}

int propagateLinkedEdits(const Garment& before, Garment& after)
{
    if (before.panels.size() != after.panels.size()) return 0;
    std::vector<char> groupDone;
    int updated = 0;
    for (std::size_t p = 0; p < after.panels.size(); ++p) {
        const Panel& now = after.panels[p];
        const Panel& was = before.panels[p];
        if (now.linkGroup <= 0 || now.linkGroup != was.linkGroup || now.points == was.points) continue;
        if (rigidlyEqual(was.points, now.points)) continue;   // simple déplacement / rotation / retournement
        if (static_cast<std::size_t>(now.linkGroup) >= groupDone.size()) groupDone.resize(static_cast<std::size_t>(now.linkGroup) + 1, 0);
        if (groupDone[static_cast<std::size_t>(now.linkGroup)]) continue;
        groupDone[static_cast<std::size_t>(now.linkGroup)] = 1;
        // Les autres pièces du groupe déjà modifiées (même opération sur plusieurs pièces) ne sont pas écrasées.
        bool othersUntouched = true;
        for (std::size_t q = 0; q < after.panels.size(); ++q)
            if (q != p && after.panels[q].linkGroup == now.linkGroup && after.panels[q].points != before.panels[q].points)
                othersUntouched = false;
        if (!othersUntouched) continue;
        updated += propagateFrom(after, static_cast<int>(p), was.points);
    }
    return updated;
}

bool lengthenPanel(Garment& g, int panel, float atY, float amount)
{
    if (panel < 0 || panel >= static_cast<int>(g.panels.size()) || amount == 0.0f) return false;
    Panel& p = g.panels[static_cast<std::size_t>(panel)];
    float lo = 1e9f, hi = -1e9f;
    for (const auto& q : p.points) {
        lo = std::min(lo, q.y);
        hi = std::max(hi, q.y);
    }
    if (atY <= lo || atY >= hi) return false;
    if (amount < 0.0f && atY - amount >= hi) return false;   // on retirerait tout le haut
    auto move = [&](float y) {
        if (y <= atY) return y;
        if (amount > 0.0f) return y + amount;
        return y + amount < atY ? atY : y + amount;
    };
    std::vector<glm::vec2> printAt;
    for (const auto& pr : p.prints) printAt.push_back(printCenter(p, pr));
    std::vector<glm::vec2> foldAt;
    for (const auto& f : p.folds)
        for (const glm::vec2 q : {f.a, f.b}) foldAt.push_back(panelBoxCenter(p) + q);
    for (auto& q : p.points) q.y = move(q.y);
    for (auto& a : g.attachments)
        if (a.base == panel) a.at.y = move(a.at.y);
    const glm::vec2 c = panelBoxCenter(p);
    for (std::size_t i = 0; i < p.prints.size(); ++i) p.prints[i].offset = glm::vec2(printAt[i].x, move(printAt[i].y)) - c;
    for (std::size_t i = 0; i < p.folds.size(); ++i) {
        p.folds[i].a = glm::vec2(foldAt[2 * i].x, move(foldAt[2 * i].y)) - c;
        p.folds[i].b = glm::vec2(foldAt[2 * i + 1].x, move(foldAt[2 * i + 1].y)) - c;
    }
    return true;
}

namespace {
int vertexNear(const std::vector<glm::vec2>& pts, glm::vec2 q, float tol)
{
    for (int k = 0; k < static_cast<int>(pts.size()); ++k)
        if (glm::distance(pts[static_cast<std::size_t>(k)], q) < tol) return k;
    return -1;
}

// Sens d'appariement réel d'une couture : true si le début de A est cousu contre la fin de B.
bool seamReversed(const Garment& g, const Seam& s)
{
    bool rev = false;
    if (s.order == 1) rev = false;
    else if (s.order == 2) rev = true;
    else {
        // Comme le mailleur : on compare les distances 3D des deux appariements, après placement.
        const MeshingOptions opt;
        auto sample = [&](int side, float t) {
            const auto edges = g.seamEdges(s, side);
            float total = 0.0f;
            for (const auto& e : edges) total += g.edgeLength(e);
            float target = t * total;
            for (const auto& e : edges) {
                const float l = g.edgeLength(e);
                if (target <= l || &e == &edges.back()) {
                    const glm::vec2 q = glm::mix(g.edgeStart(e), g.edgeEnd(e), l > 1e-9f ? glm::clamp(target / l, 0.0f, 1.0f) : 0.0f);
                    return placeGarmentPoint(g, e.panel, q, opt);
                }
                target -= l;
            }
            return glm::vec3(0.0f);
        };
        float direct = 0.0f, reversed = 0.0f;
        for (int k = 0; k <= 8; ++k) {
            const float t = static_cast<float>(k) / 8.0f;
            const glm::vec3 a = sample(0, t);
            direct += glm::distance(a, sample(1, t));
            reversed += glm::distance(a, sample(1, 1.0f - t));
        }
        rev = reversed < direct;
    }
    return s.flipped ? !rev : rev;
}

// Coupe la couture `si` au sommet `v` (position) qui est à l'intérieur du côté `side`. L'autre côté est
// coupé à la même proportion de sa longueur. Renvoie false si la coupe est impossible.
bool cutSeamAt(Garment& g, int si, int side, glm::vec2 v)
{
    Seam s = g.seams[static_cast<std::size_t>(si)];
    const bool rev = seamReversed(g, s);
    const EdgeRef xs = side == 0 ? s.a : s.b;
    const int xc = side == 0 ? s.countA : s.countB;
    const int xn = static_cast<int>(g.panels[static_cast<std::size_t>(xs.panel)].points.size());
    float before = 0.0f, total = 0.0f;
    int j = -1;
    for (int k = 0; k < xc; ++k) {
        const EdgeRef e{xs.panel, (xs.edge + k) % xn};
        if (j < 0 && glm::distance(g.edgeStart(e), v) < 1e-6f) j = k;
        if (j < 0) before += g.edgeLength(e);
        total += g.edgeLength(e);
    }
    if (j <= 0 || total <= 0.0f) return false;
    const float f = before / total;
    // Point correspondant de l'autre côté.
    const int ys = 1 - side;
    const EdgeRef yst = ys == 0 ? s.a : s.b;
    const int yc = ys == 0 ? s.countA : s.countB;
    const auto& ypts = g.panels[static_cast<std::size_t>(yst.panel)].points;
    const int yn = static_cast<int>(ypts.size());
    float ylen = 0.0f;
    for (int k = 0; k < yc; ++k) ylen += g.edgeLength({yst.panel, (yst.edge + k) % yn});
    if (ylen <= 2e-3f) return false;
    float target = glm::clamp((rev ? 1.0f - f : f) * ylen, 1e-3f, ylen - 1e-3f);
    glm::vec2 q(0.0f);
    int edge = -1;
    for (int k = 0; k < yc; ++k) {
        const EdgeRef e{yst.panel, (yst.edge + k) % yn};
        const float l = g.edgeLength(e);
        if (target <= l || k == yc - 1) {
            edge = e.edge;
            q = glm::mix(g.edgeStart(e), g.edgeEnd(e), l > 1e-9f ? glm::clamp(target / l, 0.0f, 1.0f) : 0.0f);
            break;
        }
        target -= l;
    }
    if (edge < 0) return false;
    // Un sommet intérieur tout proche (0,5 mm) sert de point de coupe ; sinon on en ajoute un.
    bool snapped = false;
    for (int k = 1; k < yc; ++k) {
        const int vi = (yst.edge + k) % yn;
        if (glm::distance(ypts[static_cast<std::size_t>(vi)], q) < 5e-4f) {
            q = ypts[static_cast<std::size_t>(vi)];
            snapped = true;
            break;
        }
    }
    if (!snapped) g.insertPoint(yst.panel, edge, q);
    // Relecture après l'ajout (les index ont pu bouger, y compris côté X sur la même pièce).
    s = g.seams[static_cast<std::size_t>(si)];
    auto split = [&](int sd, glm::vec2 at, EdgeRef& first, int& count, int& k) {
        first = sd == 0 ? s.a : s.b;
        count = sd == 0 ? s.countA : s.countB;
        const int n = static_cast<int>(g.panels[static_cast<std::size_t>(first.panel)].points.size());
        k = -1;
        for (int i = 1; i < count; ++i)
            if (glm::distance(g.panels[static_cast<std::size_t>(first.panel)].points[static_cast<std::size_t>((first.edge + i) % n)], at) < 1e-6f) k = i;
        return n;
    };
    EdgeRef xf, yf;
    int xcnt = 0, ycnt = 0, xk = -1, yk = -1;
    const int xn2 = split(side, v, xf, xcnt, xk);
    const int yn2 = split(ys, q, yf, ycnt, yk);
    if (xk <= 0 || yk <= 0) return false;
    Seam s1 = s, s2 = s;
    s1.flipped = s2.flipped = false;
    s1.order = s2.order = rev ? 2 : 1;
    EdgeRef& x1 = side == 0 ? s1.a : s1.b;
    EdgeRef& x2 = side == 0 ? s2.a : s2.b;
    int& x1c = side == 0 ? s1.countA : s1.countB;
    int& x2c = side == 0 ? s2.countA : s2.countB;
    EdgeRef& y1 = side == 0 ? s1.b : s1.a;
    EdgeRef& y2 = side == 0 ? s2.b : s2.a;
    int& y1c = side == 0 ? s1.countB : s1.countA;
    int& y2c = side == 0 ? s2.countB : s2.countA;
    x1 = xf;
    x1c = xk;
    x2 = {xf.panel, (xf.edge + xk) % xn2};
    x2c = xcnt - xk;
    // Début de X contre début de Y (ou contre la fin de Y si la couture est inversée).
    if (!rev) {
        y1 = yf;
        y1c = yk;
        y2 = {yf.panel, (yf.edge + yk) % yn2};
        y2c = ycnt - yk;
    } else {
        y1 = {yf.panel, (yf.edge + yk) % yn2};
        y1c = ycnt - yk;
        y2 = yf;
        y2c = yk;
    }
    g.seams[static_cast<std::size_t>(si)] = s1;
    g.seams.insert(g.seams.begin() + si + 1, s2);
    return true;
}

// Recale le placement d'un morceau de pièce pour qu'il reste à la même place en 3D.
void keepPlace(const Panel& before, Panel& after)
{
    const MeshingOptions opt;
    const glm::vec2 c = centroid(after.points);
    float lo = 1e9f, hi = -1e9f, y0 = 1e9f, alo = 1e9f, ahi = -1e9f, ay0 = 1e9f;
    for (const auto& q : before.points) {
        lo = std::min(lo, q.x);
        hi = std::max(hi, q.x);
        y0 = std::min(y0, q.y);
    }
    for (const auto& q : after.points) {
        alo = std::min(alo, q.x);
        ahi = std::max(ahi, q.x);
        ay0 = std::min(ay0, q.y);
    }
    const float R = std::max(after.placement.radius + 0.012f * static_cast<float>(after.layer), 0.03f);
    if (!after.placement.flat) after.placement.angleDeg += glm::degrees(((alo + ahi) - (lo + hi)) * 0.5f / R);
    after.placement.height += ay0 - y0;
    for (int it = 0; it < 2; ++it) after.placement.offset += placePoint(before, c, opt) - placePoint(after, c, opt);
}
} // namespace

bool lineCut(const Panel& panel, glm::vec2 a, glm::vec2 b, glm::vec2& p0, glm::vec2& p1)
{
    const auto& pts = panel.points;
    const int n = static_cast<int>(pts.size());
    const glm::vec2 d = b - a;
    if (n < 3 || glm::dot(d, d) < 1e-10f) return false;
    std::vector<float> ts;
    for (int k = 0; k < n; ++k) {
        const glm::vec2 p = pts[static_cast<std::size_t>(k)], q = pts[static_cast<std::size_t>((k + 1) % n)];
        const glm::vec2 e = q - p;
        const float den = d.x * e.y - d.y * e.x;
        if (std::abs(den) < 1e-12f) continue;
        const glm::vec2 w = p - a;
        const float t = (w.x * e.y - w.y * e.x) / den;   // le long de la droite
        const float u = (w.x * d.y - w.y * d.x) / den;   // le long du bord
        if (u < 0.0f || u >= 1.0f) continue;
        ts.push_back(t);
    }
    std::sort(ts.begin(), ts.end());
    ts.erase(std::unique(ts.begin(), ts.end(), [](float x, float y) { return std::abs(x - y) < 1e-6f; }), ts.end());
    // Paire d'intersections consécutives qui entoure le milieu (sinon la plus proche) et dont le milieu est dans la pièce.
    int best = -1;
    float bd = 1e9f;
    for (int k = 0; k + 1 < static_cast<int>(ts.size()); ++k) {
        const float tm = 0.5f * (ts[static_cast<std::size_t>(k)] + ts[static_cast<std::size_t>(k + 1)]);
        if (!pointInPolygon(pts, a + d * tm)) continue;
        const float dist = (ts[static_cast<std::size_t>(k)] <= 0.5f && ts[static_cast<std::size_t>(k + 1)] >= 0.5f) ? 0.0f : std::abs(tm - 0.5f);
        if (dist < bd) {
            bd = dist;
            best = k;
        }
    }
    if (best < 0) return false;
    p0 = a + d * ts[static_cast<std::size_t>(best)];
    p1 = a + d * ts[static_cast<std::size_t>(best + 1)];
    return true;
}

int splitPanelAlongLine(Garment& g, int panel, glm::vec2 a, glm::vec2 b, bool sewTogether, std::string& error)
{
    if (panel < 0 || panel >= static_cast<int>(g.panels.size())) {
        error = "Pièce inconnue.";
        return -1;
    }
    glm::vec2 p0, p1;
    if (!lineCut(g.panels[static_cast<std::size_t>(panel)], a, b, p0, p1)) {
        error = "La ligne ne traverse pas la pièce.";
        return -1;
    }
    return splitPanel(g, panel, p0, p1, sewTogether, error);
}

int splitPanel(Garment& g, int panel, glm::vec2 p0, glm::vec2 p1, bool sewTogether, std::string& error)
{
    if (panel < 0 || panel >= static_cast<int>(g.panels.size())) {
        error = "Pièce inconnue.";
        return -1;
    }
    if (g.attachmentOf(panel) >= 0) {
        error = "Pièce posée sur une autre : détachez-la avant de la couper.";
        return -1;
    }
    if (glm::distance(p0, p1) < 2e-3f) {
        error = "Ligne de coupe trop courte.";
        return -1;
    }
    {
        // La coupe doit rester à l'intérieur de la pièce.
        const auto& pts = g.panels[static_cast<std::size_t>(panel)].points;
        const int n = static_cast<int>(pts.size());
        if (!pointInPolygon(pts, 0.5f * (p0 + p1))) {
            error = "La ligne de coupe sort de la pièce.";
            return -1;
        }
        for (int k = 0; k < n; ++k) {
            const glm::vec2 a = pts[static_cast<std::size_t>(k)], b = pts[static_cast<std::size_t>((k + 1) % n)];
            const glm::vec2 d = p1 - p0, e = b - a, w = a - p0;
            const float den = d.x * e.y - d.y * e.x;
            if (std::abs(den) < 1e-12f) continue;
            const float t = (w.x * e.y - w.y * e.x) / den, u = (w.x * d.y - w.y * d.x) / den;
            if (t > 1e-3f && t < 1.0f - 1e-3f && u > 1e-4f && u < 1.0f - 1e-4f) {
                error = "La ligne de coupe croise un autre bord de la pièce.";
                return -1;
            }
        }
    }
    const Garment backup = g;
    auto failWith = [&](const char* msg) {
        g = backup;
        error = msg;
        return -1;
    };
    if (pointAt(g, panel, p0) < 0 || pointAt(g, panel, p1) < 0) return failWith("Points de coupe introuvables sur le contour.");
    {
        const auto& pts = g.panels[static_cast<std::size_t>(panel)].points;
        const int i0 = vertexNear(pts, p0, 2e-4f), i1 = vertexNear(pts, p1, 2e-4f);
        if (i0 < 0 || i1 < 0) return failWith("Points de coupe introuvables sur le contour.");
        p0 = pts[static_cast<std::size_t>(i0)];
        p1 = pts[static_cast<std::size_t>(i1)];
    }
    // Les coutures qui passent par un point de coupe sont partagées entre les deux morceaux.
    for (int guard = 0; guard < 64; ++guard) {
        bool changed = false;
        for (int si = 0; si < static_cast<int>(g.seams.size()) && !changed; ++si)
            for (int side = 0; side < 2 && !changed; ++side) {
                const Seam& s = g.seams[static_cast<std::size_t>(si)];
                const EdgeRef st = side == 0 ? s.a : s.b;
                const int cnt = side == 0 ? s.countA : s.countB;
                if (st.panel != panel) continue;
                const auto& pts = g.panels[static_cast<std::size_t>(panel)].points;
                const int n = static_cast<int>(pts.size());
                for (int k = 1; k < cnt && !changed; ++k) {
                    const glm::vec2 v = pts[static_cast<std::size_t>((st.edge + k) % n)];
                    if (v == p0 || v == p1) {
                        if (!cutSeamAt(g, si, side, v)) return failWith("Une couture qui passe par la coupe n'a pas pu être partagée.");
                        changed = true;
                    }
                }
            }
        if (!changed) break;
    }
    const Panel before = g.panels[static_cast<std::size_t>(panel)];
    const auto& pts = before.points;
    const int n = static_cast<int>(pts.size());
    const int i0 = vertexNear(pts, p0, 1e-6f), i1 = vertexNear(pts, p1, 1e-6f);
    const int len1 = ((i1 - i0) % n + n) % n + 1, len2 = ((i0 - i1) % n + n) % n + 1;
    if (i0 < 0 || i1 < 0 || len1 < 3 || len2 < 3) return failWith("La coupe doit relier deux bords différents de la pièce.");
    std::vector<glm::vec2> part1, part2;
    for (int k = 0; k < len1; ++k) part1.push_back(pts[static_cast<std::size_t>((i0 + k) % n)]);
    for (int k = 0; k < len2; ++k) part2.push_back(pts[static_cast<std::size_t>((i1 + k) % n)]);
    const int np = static_cast<int>(g.panels.size());
    // Bord e de l'ancienne pièce -> (pièce, bord) d'un des deux morceaux.
    auto mapEdge = [&](int e) {
        const int r1 = ((e - i0) % n + n) % n;
        if (r1 < len1 - 1) return EdgeRef{panel, r1};
        return EdgeRef{np, ((e - i1) % n + n) % n};
    };
    for (auto& s : g.seams)
        for (int side = 0; side < 2; ++side) {
            EdgeRef& st = side == 0 ? s.a : s.b;
            if (st.panel == panel) st = mapEdge(st.edge);
        }
    Panel first = before, second = before;
    first.points = part1;
    second.points = part2;
    second.name = before.name + " (2)";
    for (Panel* p : {&first, &second}) {
        p->linkGroup = 0;
        p->symmetric = false;
        p->prints.clear();
        p->folds.clear();
    }
    for (const auto& pr : before.prints) {
        const glm::vec2 c = printCenter(before, pr);
        Panel& dst = pointInPolygon(part2, c) ? second : first;
        Print copy = pr;
        copy.offset = c - panelBoxCenter(dst);
        dst.prints.push_back(copy);
    }
    // Lignes de pli : sur le morceau qui contient leur milieu, coupées au besoin à la ligne de coupe.
    for (const auto& f : before.folds) {
        glm::vec2 fa = panelBoxCenter(before) + f.a, fb = panelBoxCenter(before) + f.b;
        const bool inSecond = pointInPolygon(part2, 0.5f * (fa + fb));
        Panel& dst = inSecond ? second : first;
        const glm::vec2 d = p1 - p0, e = fb - fa, w = p0 - fa;
        const float den = e.x * d.y - e.y * d.x;
        if (std::abs(den) > 1e-12f) {
            const float t = (w.x * d.y - w.y * d.x) / den;   // le long du pli
            if (t > 0.0f && t < 1.0f) {
                const glm::vec2 x = fa + e * t;
                if (pointInPolygon(dst.points, 0.5f * (fa + x))) fb = x;
                else fa = x;
            }
        }
        FoldLine copy = f;
        copy.a = fa - panelBoxCenter(dst);
        copy.b = fb - panelBoxCenter(dst);
        dst.folds.push_back(copy);
    }
    keepPlace(before, first);
    keepPlace(before, second);
    g.panels[static_cast<std::size_t>(panel)] = first;
    g.panels.push_back(second);
    for (auto& a : g.attachments)
        if (a.base == panel && pointInPolygon(part2, a.at)) a.base = np;
    if (sewTogether) {
        Seam s;
        s.a = {panel, len1 - 1};    // p1 -> p0
        s.b = {np, len2 - 1};       // p0 -> p1
        s.order = 2;
        g.seams.push_back(s);
    }
    return np;
}

int addGussetStrip(Garment& g, int panel, float s0, float len, float width, std::string& error)
{
    if (panel < 0 || panel >= static_cast<int>(g.panels.size())) {
        error = "Pièce inconnue.";
        return -1;
    }
    const float total = contourLength(g, panel);
    const float L = std::min(std::abs(len), total);
    if (L < 0.01f) {
        error = "Portion de contour trop courte (1 cm au moins).";
        return -1;
    }
    if (width < 0.003f) {
        error = "Largeur de bande trop petite.";
        return -1;
    }
    const Panel& src = g.panels[static_cast<std::size_t>(panel)];
    const float dir = len >= 0.0f ? 1.0f : -1.0f;
    const glm::vec2 mid = contourPoint(g, panel, s0 + 0.5f * len);
    glm::vec2 tan2 = contourPoint(g, panel, s0 + 0.5f * len + dir * 0.005f) - contourPoint(g, panel, s0 + 0.5f * len - dir * 0.005f);
    if (glm::length(tan2) < 1e-6f) tan2 = {1.0f, 0.0f};
    tan2 = glm::normalize(tan2);
    // La bande est rangée à droite de tout le patron, à la hauteur de la pièce.
    float gx = -1e9f, py = 1e9f;
    for (const auto& pn : g.panels)
        for (const auto& q : pn.points) gx = std::max(gx, q.x);
    for (const auto& q : src.points) py = std::min(py, q.y);
    const float x0 = gx + 0.05f, y0 = py;
    Panel strip;
    strip.name = src.name + " - soufflet";
    strip.color = src.color;
    strip.finish = src.finish;
    strip.layer = src.layer;
    strip.particleDistance = src.particleDistance;
    strip.placement = src.placement;
    strip.placement.flat = true;
    strip.placement.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    strip.placement.offset = glm::vec3(0.0f);
    strip.grainDeg = 0.0f;   // droit fil le long de la bande
    strip.points = {{x0, y0}, {x0 + L, y0}, {x0 + L, y0 + width}, {x0, y0 + width}};
    {
        float lo = 1e9f, hi = -1e9f;
        for (const auto& q : src.points) {
            lo = std::min(lo, q.x);
            hi = std::max(hi, q.x);
        }
        const float R = std::max(src.placement.radius + 0.012f * static_cast<float>(src.layer), 0.03f);
        if (!src.placement.flat) strip.placement.angleDeg = src.placement.angleDeg + glm::degrees((mid.x - 0.5f * (lo + hi)) / R);
    }
    // En 3D : la bande se dresse sur la portion (longueur le long du contour, largeur vers l'extérieur).
    const MeshingOptions opt;
    const glm::vec3 P = placeGarmentPoint(g, panel, mid, opt);
    const glm::vec3 du = placeGarmentPoint(g, panel, mid + glm::vec2(0.005f, 0.0f), opt) - P;
    const glm::vec3 dv = placeGarmentPoint(g, panel, mid + glm::vec2(0.0f, 0.005f), opt) - P;
    glm::vec3 T = du * tan2.x + dv * tan2.y;
    glm::vec3 N = glm::cross(du, dv);
    {
        const ZoneFrame zf = zoneFrame(src.placement.zone, opt);
        glm::vec3 radial = P - zf.origin;
        radial -= zf.up * glm::dot(radial, zf.up);
        if (glm::dot(N, radial) > 0.0f) N = -N;   // vers le corps : la bande part derrière la pièce (fond de poche)
    }
    if (glm::length(T) > 1e-9f && glm::length(N) > 1e-9f) {
        T = glm::normalize(T);
        N = glm::normalize(N - T * glm::dot(N, T));
        const glm::vec2 c = (strip.points[0] + strip.points[2]) * 0.5f;
        const glm::vec3 pivot = placePoint(strip, c, opt);
        glm::vec3 X0 = glm::normalize(placePoint(strip, c + glm::vec2(0.01f, 0.0f), opt) - pivot);
        glm::vec3 Y0 = placePoint(strip, c + glm::vec2(0.0f, 0.01f), opt) - pivot;
        Y0 = glm::normalize(Y0 - X0 * glm::dot(Y0, X0));
        const glm::mat3 from(X0, Y0, glm::cross(X0, Y0)), to(T, N, glm::cross(T, N));
        strip.placement.rotation = glm::normalize(glm::quat_cast(to * glm::transpose(from)));
        strip.placement.offset = P + N * (0.5f * width) - pivot;
    }
    g.panels.push_back(strip);
    const int ns = static_cast<int>(g.panels.size()) - 1;
    if (sewFree(g, panel, s0, dir * L, ns, 0.0f, L) < 0) {
        g.panels.pop_back();
        error = "La bande n'a pas pu être cousue sur la pièce.";
        return -1;
    }
    return ns;
}

int sewGussetTop(Garment& g, int strip, int panel, float s0, float len)
{
    if (strip < 0 || strip >= static_cast<int>(g.panels.size())) return -1;
    const auto& pts = g.panels[static_cast<std::size_t>(strip)].points;
    if (pts.size() < 4) return -1;
    // Bande : (x0, y0) -> (x0 + L, y0) -> (x0 + L, y0 + w) -> (x0, y0 + w). Le haut part de (x0, y0 + w),
    // à la longueur 2L + w du contour, et se parcourt à reculons.
    const float L = glm::distance(pts[0], pts[1]), w = glm::distance(pts[1], pts[2]);
    return sewFree(g, strip, 2.0f * L + w, -glm::distance(pts[2], pts[3]), panel, s0, len);
}

int addZipper(Garment& g, int base, glm::vec2 a, glm::vec2 b, float width, std::string& error)
{
    if (base < 0 || base >= static_cast<int>(g.panels.size())) {
        error = "Pièce inconnue.";
        return -1;
    }
    if (g.attachmentOf(base) >= 0) {
        error = "Une fermeture se pose sur une pièce ordinaire, pas sur une pièce déjà posée.";
        return -1;
    }
    const Panel& bp = g.panels[static_cast<std::size_t>(base)];
    // Partie du segment à l'intérieur de la pièce, raccourcie de 5 mm à chaque bout qui touche le bord.
    glm::vec2 p0, p1;
    if (!lineCut(bp, a, b, p0, p1)) {
        error = "La fermeture doit être tracée sur la pièce.";
        return -1;
    }
    const glm::vec2 dir0 = glm::normalize(b - a);
    if (glm::dot(p1 - p0, dir0) < 0.0f) std::swap(p0, p1);
    if (!pointInPolygon(bp.points, a) || glm::dot(a - p0, dir0) < 0.0f) a = p0 + dir0 * 0.005f;
    if (!pointInPolygon(bp.points, b) || glm::dot(p1 - b, dir0) < 0.0f) b = p1 - dir0 * 0.005f;
    const float L = glm::distance(a, b);
    if (L < 0.02f) {
        error = "Fermeture trop courte (2 cm au moins).";
        return -1;
    }
    width = std::clamp(width, 0.008f, 0.08f);
    const glm::vec2 d = (b - a) / L, n(-d.y, d.x);
    float gx = -1e9f, gy = 1e9f;
    for (const auto& pn : g.panels)
        for (const auto& q : pn.points) gx = std::max(gx, q.x);
    for (const auto& q : bp.points) gy = std::min(gy, q.y);
    // Dessinées à droite du patron ; posées sur la base en 3D.
    const glm::vec2 park(gx + 0.05f + 0.5f * (std::abs(d.x) * L + std::abs(n.x) * width), gy + 0.5f * (std::abs(d.y) * L + std::abs(n.y) * width));
    Panel zip;
    zip.name = "Fermeture éclair";
    zip.color = glm::vec3(0.12f, 0.12f, 0.13f);
    zip.finish = FabricFinish::Zipper;
    zip.placement = bp.placement;
    zip.stiffness = 2.0f;
    zip.grainDeg = glm::degrees(std::atan2(d.y, d.x));
    const glm::vec2 hl = d * (0.5f * L), hw = n * (0.5f * width);
    zip.points = {park - hl - hw, park + hl - hw, park + hl + hw, park - hl + hw};
    if (signedArea(zip.points) < 0.0f) std::reverse(zip.points.begin(), zip.points.end());
    Panel puller;
    puller.name = "Curseur";
    puller.color = glm::vec3(0.58f, 0.59f, 0.61f);
    puller.finish = FabricFinish::Reflective;
    puller.placement = bp.placement;
    puller.stiffness = 4.0f;
    {
        // Curseur + tirette qui pend : 1,4 x 3,2 cm, cousu par le haut (au bout de la fermeture).
        const glm::vec2 o(park.x + 0.5f * (std::abs(d.x) * L + std::abs(n.x) * width) + 0.03f, park.y);
        const float s = 0.01f;
        for (const glm::vec2 q : {glm::vec2(-0.7f, 0.0f), glm::vec2(0.7f, 0.0f), glm::vec2(0.7f, -2.3f), glm::vec2(0.4f, -3.2f),
                                  glm::vec2(-0.4f, -3.2f), glm::vec2(-0.7f, -2.3f)})
            puller.points.push_back(o + q * s);
    }
    g.panels.push_back(zip);
    const int zi = static_cast<int>(g.panels.size()) - 1;
    if (g.attachPatch(zi, base, 0.5f * (a + b), AttachKind::AllRound) < 0) {
        g.panels.pop_back();
        error = "La fermeture n'a pas pu être posée sur la pièce.";
        return -1;
    }
    g.panels.push_back(puller);
    const int pi = static_cast<int>(g.panels.size()) - 1;
    if (g.attachPatch(pi, base, b - glm::vec2(0.0f, 0.016f), AttachKind::Flap) < 0) g.removePanel(pi);
    return zi;
}

} // namespace drapeo::garment
