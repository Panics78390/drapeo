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

const char* fabricFinishName(FabricFinish f)
{
    switch (f) {
    case FabricFinish::Plain: return "Uni";
    case FabricFinish::Ripstop: return "Toile technique (ripstop)";
    case FabricFinish::Twill: return "Sergé (denim)";
    case FabricFinish::Reflective: return "Réfléchissant";
    case FabricFinish::Zipper: return "Fermeture éclair";
    }
    return "?";
}

glm::vec2 panelBoxCenter(const Panel& panel)
{
    if (panel.points.empty()) return glm::vec2(0.0f);
    glm::vec2 lo(1e9f), hi(-1e9f);
    for (const auto& p : panel.points) {
        lo = glm::min(lo, p);
        hi = glm::max(hi, p);
    }
    return (lo + hi) * 0.5f;
}

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

void Garment::removeGroup(int group)
{
    if (group < 0 || group >= static_cast<int>(groups.size())) return;
    groups.erase(groups.begin() + group);
    for (auto& p : panels) {
        if (p.group == group) p.group = -1;
        else if (p.group > group) --p.group;
    }
}

void Garment::moveGroup(int from, int to)
{
    const int n = static_cast<int>(groups.size());
    if (from < 0 || from >= n || to < 0 || to >= n || from == to) return;
    const PieceGroup moved = groups[static_cast<std::size_t>(from)];
    groups.erase(groups.begin() + from);
    groups.insert(groups.begin() + to, moved);
    for (auto& p : panels) {
        if (p.group == from) p.group = to;
        else if (from < to && p.group > from && p.group <= to) --p.group;
        else if (to < from && p.group >= to && p.group < from) ++p.group;
    }
}

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
    auto& pn = g.panels[static_cast<std::size_t>(panel)];
    pn.symAxisX = map({pn.symAxisX, (lo.y + hi.y) * 0.5f}).x;
}

namespace {
// --- Opérations booléennes (Greiner-Hormann, avec léger décalage en cas de cas dégénéré) ---
struct GhNode {
    glm::dvec2 p;
    int next = -1, prev = -1, neighbor = -1;
    bool inter = false, entry = false, visited = false;
    double alpha = 0.0;
};

bool insideD(const std::vector<glm::dvec2>& poly, glm::dvec2 p)
{
    bool in = false;
    const std::size_t n = poly.size();
    for (std::size_t i = 0, j = n - 1; i < n; j = i++)
        if ((poly[i].y > p.y) != (poly[j].y > p.y) &&
            p.x < (poly[j].x - poly[i].x) * (p.y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x)
            in = !in;
    return in;
}

double areaD(const std::vector<glm::dvec2>& p)
{
    double a = 0.0;
    for (std::size_t i = 0; i < p.size(); ++i) {
        const auto& u = p[i];
        const auto& v = p[(i + 1) % p.size()];
        a += u.x * v.y - v.x * u.y;
    }
    return a * 0.5;
}

// Renvoie false si un cas dégénéré (sommet sur un bord, bords superposés) est rencontré.
bool ghRun(std::vector<glm::dvec2> A, std::vector<glm::dvec2> B, bool subtract, std::vector<std::vector<glm::dvec2>>& out,
           bool& anyIntersection)
{
    out.clear();
    if (areaD(A) < 0) std::reverse(A.begin(), A.end());
    if (areaD(B) < 0) std::reverse(B.begin(), B.end());
    const int na = static_cast<int>(A.size()), nb = static_cast<int>(B.size());
    // Intersections bord à bord
    struct Hit { int ea, eb; double ta, tb; glm::dvec2 p; };
    std::vector<Hit> hits;
    constexpr double kEps = 1e-9;
    for (int i = 0; i < na; ++i) {
        const glm::dvec2 a0 = A[static_cast<std::size_t>(i)], a1 = A[static_cast<std::size_t>((i + 1) % na)];
        for (int j = 0; j < nb; ++j) {
            const glm::dvec2 b0 = B[static_cast<std::size_t>(j)], b1 = B[static_cast<std::size_t>((j + 1) % nb)];
            const glm::dvec2 r = a1 - a0, s = b1 - b0;
            const double den = r.x * s.y - r.y * s.x;
            const glm::dvec2 q = b0 - a0;
            const double scale = glm::length(r) * glm::length(s);
            if (std::abs(den) <= 1e-12 * scale) {   // parallèles
                const double dist = std::abs(q.x * r.y - q.y * r.x) / std::max(glm::length(r), 1e-12);
                if (dist < 1e-7) {   // colinéaires : superposés ?
                    const double rr = glm::dot(r, r);
                    const double t0 = glm::dot(q, r) / rr, t1 = glm::dot(b1 - a0, r) / rr;
                    if (std::max(t0, t1) > -kEps && std::min(t0, t1) < 1.0 + kEps) return false;
                }
                continue;
            }
            const double t = (q.x * s.y - q.y * s.x) / den;
            const double u = (q.x * r.y - q.y * r.x) / den;
            if (t < -kEps || t > 1.0 + kEps || u < -kEps || u > 1.0 + kEps) continue;
            if (t < 1e-7 || t > 1.0 - 1e-7 || u < 1e-7 || u > 1.0 - 1e-7) return false;   // passe par un sommet
            hits.push_back({i, j, t, u, a0 + r * t});
        }
    }
    anyIntersection = !hits.empty();
    if (hits.empty()) return true;
    // Listes chaînées des deux contours avec les intersections insérées
    std::vector<GhNode> nodes;
    auto build = [&](const std::vector<glm::dvec2>& P, bool isA) {
        const int n = static_cast<int>(P.size());
        const int start = static_cast<int>(nodes.size());
        std::vector<int> order;
        for (int e = 0; e < n; ++e) {
            GhNode v;
            v.p = P[static_cast<std::size_t>(e)];
            nodes.push_back(v);
            order.push_back(static_cast<int>(nodes.size()) - 1);
            std::vector<std::pair<double, int>> onEdge;
            for (int h = 0; h < static_cast<int>(hits.size()); ++h)
                if ((isA ? hits[static_cast<std::size_t>(h)].ea : hits[static_cast<std::size_t>(h)].eb) == e)
                    onEdge.push_back({isA ? hits[static_cast<std::size_t>(h)].ta : hits[static_cast<std::size_t>(h)].tb, h});
            std::sort(onEdge.begin(), onEdge.end());
            for (const auto& [t, h] : onEdge) {
                GhNode x;
                x.p = hits[static_cast<std::size_t>(h)].p;
                x.inter = true;
                x.alpha = t;
                x.neighbor = h;   // provisoire : index du croisement
                nodes.push_back(x);
                order.push_back(static_cast<int>(nodes.size()) - 1);
            }
        }
        for (std::size_t k = 0; k < order.size(); ++k) {
            nodes[static_cast<std::size_t>(order[k])].next = order[(k + 1) % order.size()];
            nodes[static_cast<std::size_t>(order[k])].prev = order[(k + order.size() - 1) % order.size()];
        }
        return start;
    };
    const int startA = build(A, true);
    const int startB = build(B, false);
    // Relie chaque croisement à son jumeau
    std::vector<int> hitA(hits.size(), -1), hitB(hits.size(), -1);
    for (int k = 0; k < static_cast<int>(nodes.size()); ++k) {
        if (!nodes[static_cast<std::size_t>(k)].inter) continue;
        const int h = nodes[static_cast<std::size_t>(k)].neighbor;
        (k < startB ? hitA : hitB)[static_cast<std::size_t>(h)] = k;
    }
    for (std::size_t h = 0; h < hits.size(); ++h) {
        nodes[static_cast<std::size_t>(hitA[h])].neighbor = hitB[h];
        nodes[static_cast<std::size_t>(hitB[h])].neighbor = hitA[h];
    }
    // Entrée / sortie. Différence A - B = A ∩ (complément de B) : on inverse B.
    // Réunion = complément de (cA ∩ cB) : on inverse les deux.
    auto mark = [&](int start, const std::vector<glm::dvec2>& other, bool invert) {
        bool status = !insideD(other, nodes[static_cast<std::size_t>(start)].p);
        if (invert) status = !status;
        int k = start;
        do {
            auto& nd = nodes[static_cast<std::size_t>(k)];
            if (nd.inter) {
                nd.entry = status;
                status = !status;
            }
            k = nd.next;
        } while (k != start);
    };
    mark(startA, B, true);
    mark(startB, A, !subtract);
    (void)startA;
    for (int k = 0; k < static_cast<int>(nodes.size()); ++k) {
        if (!nodes[static_cast<std::size_t>(k)].inter || nodes[static_cast<std::size_t>(k)].visited) continue;
        std::vector<glm::dvec2> poly;
        int cur = k;
        int guard = 0;
        do {
            nodes[static_cast<std::size_t>(cur)].visited = true;
            nodes[static_cast<std::size_t>(nodes[static_cast<std::size_t>(cur)].neighbor)].visited = true;
            const bool forward = nodes[static_cast<std::size_t>(cur)].entry;
            poly.push_back(nodes[static_cast<std::size_t>(cur)].p);
            do {
                cur = forward ? nodes[static_cast<std::size_t>(cur)].next : nodes[static_cast<std::size_t>(cur)].prev;
                if (!nodes[static_cast<std::size_t>(cur)].inter) poly.push_back(nodes[static_cast<std::size_t>(cur)].p);
            } while (!nodes[static_cast<std::size_t>(cur)].inter && ++guard < 100000);
            cur = nodes[static_cast<std::size_t>(cur)].neighbor;
        } while (!nodes[static_cast<std::size_t>(cur)].visited && ++guard < 100000);
        if (guard >= 100000) return false;
        if (poly.size() >= 3 && std::abs(areaD(poly)) > 1e-10) out.push_back(poly);
    }
    return true;
}
} // namespace

std::vector<std::vector<glm::vec2>> polygonBoolean(const std::vector<glm::vec2>& a, const std::vector<glm::vec2>& b,
                                                   bool subtract, bool* touching)
{
    std::vector<glm::dvec2> A, B;
    for (const auto& q : a) A.emplace_back(q);
    for (const auto& q : b) B.emplace_back(q);
    auto clean = [](const std::vector<std::vector<glm::dvec2>>& polys) {
        std::vector<std::vector<glm::vec2>> res;
        for (const auto& p : polys) {
            std::vector<glm::vec2> q;
            for (const auto& v : p) {
                const glm::vec2 f(v);
                if (q.empty() || glm::distance(q.back(), f) > 2e-5f) q.push_back(f);
            }
            while (q.size() > 3 && glm::distance(q.front(), q.back()) <= 2e-5f) q.pop_back();
            if (q.size() >= 3 && std::abs(signedArea(q)) > 1e-9f) res.push_back(q);
        }
        return res;
    };
    std::vector<std::vector<glm::vec2>> best;
    bool bestTouch = true, found = false;
    std::size_t bestScore = 0;
    // Cas dégénérés (sommets sur un bord, bords communs) : on décale très légèrement la forme B
    // dans plusieurs directions et on garde le résultat le plus simple (une seule pièce, peu de points).
    for (int attempt = 0; attempt < 17; ++attempt) {
        std::vector<glm::dvec2> Bs = B;
        if (attempt > 0) {
            const double ang = (attempt - 1) * (2.0 * 3.14159265358979 / 16.0) + 0.1;
            const glm::dvec2 d(std::cos(ang) * 2e-6, std::sin(ang) * 2e-6);
            for (auto& q : Bs) q += d;
        }
        std::vector<std::vector<glm::dvec2>> out;
        bool anyHit = false, touch = true;
        if (!ghRun(A, Bs, subtract, out, anyHit)) continue;
        if (!anyHit) {   // pas de croisement : inclusion ou formes séparées
            const bool bInA = insideD(A, Bs[0]), aInB = insideD(Bs, A[0]);
            out.clear();
            if (subtract) {
                if (aInB) { /* tout est découpé */ }
                else if (bInA) { out.push_back(A); out.push_back(Bs); }   // trou
                else { out.push_back(A); touch = false; }
            } else {
                if (aInB) out.push_back(Bs);
                else if (bInA) out.push_back(A);
                else { out.push_back(A); out.push_back(Bs); touch = false; }
            }
        }
        auto res = clean(out);
        std::size_t pts = 0;
        for (const auto& p : res) pts += p.size();
        const std::size_t score = (res.size() == 1 && touch ? 0 : 1000000) + (res.empty() ? 500000 : 0) + pts;
        if (!found || score < bestScore) {
            best = res;
            bestTouch = touch;
            bestScore = score;
            found = true;
        }
        if (attempt == 0) break;   // cas normal : pas besoin de décaler
    }
    if (touching) *touching = bestTouch;
    return best;
}

int symmetricPartner(const Panel& panel, int point)
{
    const int n = static_cast<int>(panel.points.size());
    if (!panel.symmetric || point < 0 || point >= n) return -1;
    const glm::vec2 p = panel.points[static_cast<std::size_t>(point)];
    if (std::abs(p.x - panel.symAxisX) < 0.0015f) return -1;   // sur l'axe
    const glm::vec2 m(2.0f * panel.symAxisX - p.x, p.y);
    int best = -1;
    float bd = 0.004f;
    for (int j = 0; j < n; ++j) {
        if (j == point) continue;
        const float d = glm::distance(panel.points[static_cast<std::size_t>(j)], m);
        if (d < bd) {
            bd = d;
            best = j;
        }
    }
    return best;
}

void replaceOutline(Garment& g, int target, std::vector<glm::vec2> pts, const std::vector<int>* hint)
{
    if (target < 0 || target >= static_cast<int>(g.panels.size()) || pts.size() < 3) return;
    const auto old = g.panels[static_cast<std::size_t>(target)].points;
    // On garde exactement les anciens points (les calculs les déplacent d'un rien).
    for (auto& q : pts)
        for (const auto& o : old)
            if (glm::distance(q, o) < 2e-5f) q = o;
    bool reversed = false;
    if ((signedArea(pts) > 0.0f) != (signedArea(old) > 0.0f)) {
        std::reverse(pts.begin(), pts.end());
        reversed = true;
    }
    // Correspondance anciens points -> nouveaux, pour garder les coutures.
    const int n = static_cast<int>(old.size()), m = static_cast<int>(pts.size());
    std::vector<int> newIndex(static_cast<std::size_t>(n), -1);
    std::vector<char> viaHint(static_cast<std::size_t>(n), 0);
    for (int k = 0; k < n; ++k) {
        for (int j = 0; j < m; ++j)
            if (pts[static_cast<std::size_t>(j)] == old[static_cast<std::size_t>(k)]) {
                newIndex[static_cast<std::size_t>(k)] = j;
                break;
            }
        if (newIndex[static_cast<std::size_t>(k)] < 0 && hint != nullptr && k < static_cast<int>(hint->size()) &&
            (*hint)[static_cast<std::size_t>(k)] >= 0) {
            const int h = (*hint)[static_cast<std::size_t>(k)];
            newIndex[static_cast<std::size_t>(k)] = reversed ? m - 1 - h : h;
            viaHint[static_cast<std::size_t>(k)] = 1;
        }
    }
    // Ancien bord e -> (premier nouveau bord, nombre de bords) ; un bord coupé par de nouveaux points
    // alignés donne plusieurs bords.
    auto mapEdge = [&](int e, int& count) {
        const int a = newIndex[static_cast<std::size_t>(e)], b = newIndex[static_cast<std::size_t>((e + 1) % n)];
        count = 0;
        if (a < 0 || b < 0) return -1;
        const int steps = ((b - a) % m + m) % m;
        if (steps == 0 || steps > m / 2 + 1) return -1;
        const glm::vec2 p0 = pts[static_cast<std::size_t>(a)], p1 = pts[static_cast<std::size_t>(b)];
        // Côté reconstruit en miroir : les points ajoutés en face sont acceptés même s'ils ont bougé.
        const bool mirrored = viaHint[static_cast<std::size_t>(e)] || viaHint[static_cast<std::size_t>((e + 1) % n)];
        for (int k = 1; k < steps && !(mirrored && steps <= 4); ++k)
            if (distanceToSegment(pts[static_cast<std::size_t>((a + k) % m)], p0, p1) > 1e-4f) return -1;
        count = steps;
        return a;
    };
    std::erase_if(g.seams, [&](Seam& s) {
        for (int side = 0; side < 2; ++side) {
            EdgeRef& st = side == 0 ? s.a : s.b;
            if (st.panel != target) continue;
            int& cnt = side == 0 ? s.countA : s.countB;
            int c0 = 0;
            const int first = mapEdge(st.edge, c0);
            if (first < 0) return true;
            int total = c0;
            for (int k = 1; k < cnt; ++k) {
                int ck = 0;
                if (mapEdge((st.edge + k) % n, ck) != (first + total) % m) return true;
                total += ck;
            }
            st.edge = first;
            cnt = total;
        }
        return false;
    });
    for (auto& a : g.attachments)
        if (a.patch == target && (a.startEdge >= m || a.count > m)) {   // pièce posée : bords cousus à recalculer
            a.startEdge = 0;
            a.count = 0;
        }
    g.panels[static_cast<std::size_t>(target)].points = std::move(pts);
}

std::vector<glm::vec2> symmetrizedOutline(const Panel& panel, bool keepLeft)
{
    const auto& pts = panel.points;
    if (pts.size() < 3) return {};
    const float ax = panel.symAxisX;
    float y0 = 1e9f, y1 = -1e9f, x0 = 1e9f, x1 = -1e9f;
    for (const auto& q : pts) {
        y0 = std::min(y0, q.y);
        y1 = std::max(y1, q.y);
        x0 = std::min(x0, q.x);
        x1 = std::max(x1, q.x);
    }
    const float big = (x1 - x0) + (y1 - y0) + 1.0f;
    // Moitié gardée : on retire tout ce qui dépasse de l'autre côté de l'axe.
    const std::vector<glm::vec2> other = keepLeft
        ? std::vector<glm::vec2>{{ax, y0 - 1}, {ax + big, y0 - 1}, {ax + big, y1 + 1}, {ax, y1 + 1}}
        : std::vector<glm::vec2>{{ax - big, y0 - 1}, {ax, y0 - 1}, {ax, y1 + 1}, {ax - big, y1 + 1}};
    auto halves = polygonBoolean(pts, other, true);
    if (halves.empty()) return {};
    std::size_t bi = 0;
    for (std::size_t k = 1; k < halves.size(); ++k)
        if (std::abs(signedArea(halves[k])) > std::abs(signedArea(halves[bi]))) bi = k;
    auto half = halves[bi];
    if (signedArea(half) < 0.0f) std::reverse(half.begin(), half.end());
    for (auto& q : half)
        if (std::abs(q.x - ax) < 2e-5f) q.x = ax;
    for (auto& q : half)   // points d'origine conservés exactement
        for (const auto& o : pts)
            if (glm::distance(q, o) < 2e-5f) q = o;
    // Le bord posé sur l'axe (il doit y en avoir exactement un)
    const int n = static_cast<int>(half.size());
    int axisEdge = -1, count = 0;
    for (int e = 0; e < n; ++e) {
        const glm::vec2 a = half[static_cast<std::size_t>(e)], b = half[static_cast<std::size_t>((e + 1) % n)];
        if (std::abs(a.x - ax) < 2e-5f && std::abs(b.x - ax) < 2e-5f && std::abs(a.y - b.y) > 1e-5f) {
            axisEdge = e;
            ++count;
        }
    }
    if (count != 1) return {};
    // Chaîne hors de l'axe : de l'extrémité du bord d'axe jusqu'à son début
    std::vector<glm::vec2> chain;
    for (int k = 0; k < n; ++k) chain.push_back(half[static_cast<std::size_t>((axisEdge + 1 + k) % n)]);
    std::vector<glm::vec2> full = chain;
    for (int k = static_cast<int>(chain.size()) - 2; k >= 1; --k) {
        const glm::vec2 q = chain[static_cast<std::size_t>(k)];
        full.push_back({2.0f * ax - q.x, q.y});
    }
    // Points d'axe ajoutés par le découpage et alignés avec leurs voisins : inutiles
    auto isOld = [&](glm::vec2 q) {
        for (const auto& o : pts)
            if (glm::distance(o, q) < 2e-5f) return true;
        return false;
    };
    for (std::size_t k = 0; k < full.size() && full.size() > 3;) {
        const glm::vec2 a = full[(k + full.size() - 1) % full.size()], q = full[k], b = full[(k + 1) % full.size()];
        const glm::vec2 u = q - a, v = b - q;
        if (std::abs(q.x - ax) < 2e-5f && !isOld(q) && std::abs(u.x * v.y - u.y * v.x) < 1e-9f && glm::dot(u, v) > 0.0f)
            full.erase(full.begin() + static_cast<std::ptrdiff_t>(k));
        else
            ++k;
    }
    if ((signedArea(full) < 0.0f) != (signedArea(pts) < 0.0f)) std::reverse(full.begin(), full.end());
    return full;
}

bool symmetrizePanel(Garment& g, int panel, bool keepLeft)
{
    if (panel < 0 || panel >= static_cast<int>(g.panels.size()) || !g.panels[static_cast<std::size_t>(panel)].symmetric) return false;
    const auto& pn = g.panels[static_cast<std::size_t>(panel)];
    auto full = symmetrizedOutline(pn, keepLeft);
    if (full.size() < 3) return false;
    // Côté reconstruit : chaque ancien point y devient le miroir de son ancien partenaire, pour
    // que les coutures de ce côté suivent.
    const auto& old = pn.points;
    const float ax = pn.symAxisX;
    std::vector<int> hint(old.size(), -1);
    for (std::size_t k = 0; k < old.size(); ++k) {
        const bool slave = keepLeft ? old[k].x > ax + 1e-5f : old[k].x < ax - 1e-5f;
        if (!slave) continue;
        const glm::vec2 want(2.0f * ax - old[k].x, old[k].y);
        int partner = -1;
        float bd = 0.004f;
        for (std::size_t j = 0; j < old.size(); ++j)
            if (const float d = glm::distance(old[j], want); d < bd) {
                bd = d;
                partner = static_cast<int>(j);
            }
        if (partner < 0) continue;
        const glm::vec2 mp(2.0f * ax - old[static_cast<std::size_t>(partner)].x, old[static_cast<std::size_t>(partner)].y);
        for (std::size_t j = 0; j < full.size(); ++j)
            if (glm::distance(full[j], mp) < 2e-5f) {
                hint[k] = static_cast<int>(j);
                break;
            }
    }
    replaceOutline(g, panel, std::move(full), &hint);
    return true;
}

bool combinePanels(Garment& g, int target, int shape, bool subtract, std::string& error)
{
    const int np = static_cast<int>(g.panels.size());
    if (target < 0 || shape < 0 || target >= np || shape >= np || target == shape) {
        error = "Choisissez deux pièces différentes.";
        return false;
    }
    const auto& old = g.panels[static_cast<std::size_t>(target)].points;
    bool touching = true;
    auto result = polygonBoolean(old, g.panels[static_cast<std::size_t>(shape)].points, subtract, &touching);
    if (!touching) {
        error = subtract ? "La forme ne touche pas la pièce." : "Les deux formes doivent se toucher pour être réunies.";
        return false;
    }
    if (result.empty()) {
        error = "La découpe couvre toute la pièce.";
        return false;
    }
    if (result.size() > 1) {
        error = subtract ? "La découpe ferait un trou ou couperait la pièce en deux : faites-la déborder d'un seul bord."
                         : "Les deux formes doivent se toucher pour être réunies.";
        return false;
    }
    replaceOutline(g, target, std::move(result[0]));
    g.removePanel(shape);
    return true;
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

bool Garment::toggleAttachedEdge(int attachment, int edge)
{
    if (attachment < 0 || attachment >= static_cast<int>(attachments.size())) return false;
    Attachment& a = attachments[static_cast<std::size_t>(attachment)];
    if (a.patch < 0 || a.patch >= static_cast<int>(panels.size())) return false;
    const int n = static_cast<int>(panels[static_cast<std::size_t>(a.patch)].points.size());
    if (edge < 0 || edge >= n || n < 2) return false;
    const bool all = a.count <= 0 || a.count >= n;
    const int start = all ? 0 : ((a.startEdge % n) + n) % n, count = all ? n : a.count;
    auto sewn = [&](int e) { return ((e - start) % n + n) % n < count; };
    auto normalize = [&]() {
        if (a.count >= n) {
            a.startEdge = 0;
            a.count = 0;   // tout le tour
        }
    };
    if (sewn(edge)) {
        if (all) {   // première ouverture : tout sauf ce bord
            a.startEdge = (edge + 1) % n;
            a.count = n - 1;
            return true;
        }
        if (count <= 1) return false;   // il ne resterait rien de cousu
        if (edge == start) {
            a.startEdge = (start + 1) % n;
            a.count = count - 1;
            return true;
        }
        if (edge == (start + count - 1) % n) {
            a.count = count - 1;
            return true;
        }
        return false;   // ouvrir au milieu ferait deux ouvertures
    }
    if (edge == (start + count) % n) {
        a.startEdge = start;
        a.count = count + 1;
        normalize();
        return true;
    }
    if (edge == (start - 1 + n) % n) {
        a.startEdge = edge;
        a.count = count + 1;
        normalize();
        return true;
    }
    return false;
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
    for (auto& pr : copy.prints) {
        pr.offset.x = -pr.offset.x;
        pr.angleDeg = -pr.angleDeg;
    }
    for (auto& f : copy.folds) {
        f.a.x = -f.a.x;
        f.b.x = -f.b.x;
    }
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
