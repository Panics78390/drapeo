#include "garment/PatternOps.h"

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
        for (auto& q : pn.points) q = c + linkTransform(q - c, degrees, false);
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

} // namespace drapeo::garment
