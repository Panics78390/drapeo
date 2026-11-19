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
