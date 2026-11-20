#include "garment/GarmentMesher.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>

#include <CDT.h>

namespace drapeo::garment {

namespace {
struct PanelFrame {
    float cx = 0.0f, minY = 0.0f, maxY = 0.0f;
};

PanelFrame frameOf(const Panel& p)
{
    float lo = 1e9f, hi = -1e9f, minY = 1e9f, maxY = -1e9f;
    for (const auto& q : p.points) {
        lo = std::min(lo, q.x);
        hi = std::max(hi, q.x);
        minY = std::min(minY, q.y);
        maxY = std::max(maxY, q.y);
    }
    return {(lo + hi) * 0.5f, minY, maxY};
}
} // namespace

static void limbAxis(BodyZone zone, const BodyAxes& axes, glm::vec3& top, glm::vec3& bottom)
{
    const int side = (zone == BodyZone::LeftArm || zone == BodyZone::LeftLeg) ? 0 : 1;
    if (zone == BodyZone::LeftArm || zone == BodyZone::RightArm) {
        top = axes.armTop[side];
        bottom = axes.armBottom[side];
    } else {
        top = axes.legTop[side];
        bottom = axes.legBottom[side];
    }
}

ZoneFrame zoneFrame(BodyZone zone, const MeshingOptions& options)
{
    ZoneFrame f;
    f.origin = options.bodyCenter + glm::vec3(options.axes.torsoCenter.x, 0.0f, options.axes.torsoCenter.z) * options.bodyScale;
    if (zone == BodyZone::Torso) return f;
    f.origin = options.bodyCenter;
    glm::vec3 top, bottom;
    limbAxis(zone, options.axes, top, bottom);
    f.origin += bottom * options.bodyScale;
    f.up = glm::normalize(top - bottom);
    f.front = glm::normalize(glm::vec3(0, 0, 1) - f.up * f.up.z);
    f.side = glm::cross(f.up, f.front);
    return f;
}

static glm::vec3 wrapPoint(const Panel& panel, const PanelFrame& f, glm::vec2 p, const MeshingOptions& options)
{
    const float R = std::max((panel.placement.radius + 0.012f * static_cast<float>(panel.layer)) * options.bodyScale, 0.03f);   // couche : posée par-dessus
    const float u = p.x - f.cx;           // abscisse le long du tour du corps
    const float v = p.y - f.minY;         // hauteur dans la pièce
    const bool flat = panel.placement.flat;
    const float a0 = glm::radians(panel.placement.angleDeg);
    const float a = flat ? a0 : a0 + u / R;
    // Pièce plate : posée sur le plan tangent au cylindre (devant lui partout), sans courbure.
    const float along = flat ? u : 0.0f;
    if (panel.placement.zone == BodyZone::Torso) {
        const glm::vec3 tc = glm::vec3(options.axes.torsoCenter.x, 0.0f, options.axes.torsoCenter.z) * options.bodyScale;
        return options.bodyCenter + tc +
               glm::vec3(R * std::sin(a) + along * std::cos(a), panel.placement.height * options.bodyScale + v,
                         R * std::cos(a) - along * std::sin(a));
    }

    // Membre : cylindre le long de l'axe incliné ; le bas de la pièce est posé à
    // l'endroit de l'axe qui est à la hauteur `height`.
    glm::vec3 top, bottom;
    limbAxis(panel.placement.zone, options.axes, top, bottom);
    top *= options.bodyScale;
    bottom *= options.bodyScale;
    const glm::vec3 up = glm::normalize(top - bottom);
    const float h0 = panel.placement.height * options.bodyScale;
    const float t = (h0 - bottom.y) / std::max(top.y - bottom.y, 1e-4f);
    const glm::vec3 base = bottom + (top - bottom) * t;
    const glm::vec3 front = glm::normalize(glm::vec3(0, 0, 1) - up * up.z);
    const glm::vec3 side = glm::cross(up, front);
    return options.bodyCenter + base + up * v + R * (std::sin(a) * side + std::cos(a) * front) +
           along * (std::cos(a) * side - std::sin(a) * front);
}

glm::vec3 panelPivot(const Panel& panel, const MeshingOptions& options)
{
    const PanelFrame f = frameOf(panel);
    return wrapPoint(panel, f, {f.cx, (f.minY + f.maxY) * 0.5f}, options);
}

glm::vec3 placePoint(const Panel& panel, glm::vec2 p, const MeshingOptions& options)
{
    const PanelFrame f = frameOf(panel);
    const glm::vec3 w = wrapPoint(panel, f, p, options);
    const auto& pl = panel.placement;
    const bool identity = std::abs(pl.rotation.w) > 0.999999f;
    if (identity) return w + pl.offset;
    const glm::vec3 pivot = wrapPoint(panel, f, {f.cx, (f.minY + f.maxY) * 0.5f}, options);
    return pivot + glm::normalize(pl.rotation) * (w - pivot) + pl.offset;
}

glm::vec3 placeGarmentPoint(const Garment& garment, int pi, glm::vec2 q, const MeshingOptions& options)
{
    const Panel& panel = garment.panels[static_cast<std::size_t>(pi)];
    const int ai = garment.attachmentOf(pi);
    if (ai < 0) return placePoint(panel, q, options);
    const Attachment& att = garment.attachments[static_cast<std::size_t>(ai)];
    if (att.base < 0 || att.base >= static_cast<int>(garment.panels.size()) || att.base == pi) return placePoint(panel, q, options);
    // Posée à plat sur la base, 6 mm au-dessus (vers l'extérieur).
    const Panel& bp = garment.panels[static_cast<std::size_t>(att.base)];
    glm::vec2 lo(1e9f), hi(-1e9f);
    for (const auto& p : panel.points) {
        lo = glm::min(lo, p);
        hi = glm::max(hi, p);
    }
    const glm::vec2 m = q + att.at - (lo + hi) * 0.5f;
    const glm::vec3 p0 = placePoint(bp, m, options);
    const glm::vec3 du = placePoint(bp, m + glm::vec2(0.01f, 0.0f), options) - p0;
    const glm::vec3 dv = placePoint(bp, m + glm::vec2(0.0f, 0.01f), options) - p0;
    glm::vec3 nrm = glm::cross(du, dv);
    const ZoneFrame zf = zoneFrame(bp.placement.zone, options);
    glm::vec3 radial = p0 - zf.origin;
    radial -= zf.up * glm::dot(radial, zf.up);
    if (glm::dot(nrm, radial) < 0.0f) nrm = -nrm;
    const float l = glm::length(nrm);
    return p0 + (l > 1e-9f ? nrm / l : glm::vec3(0.0f)) * 0.006f;
}

std::vector<std::array<uint32_t, 2>> enabledSeamPairs(const Garment& garment, const GarmentMesh& mesh)
{
    std::vector<std::array<uint32_t, 2>> out;
    for (std::size_t k = 0; k < mesh.allSeamPairs.size(); ++k) {
        const int s = mesh.seamOfPair[k];
        if (s < 0 || (s < static_cast<int>(garment.seams.size()) && garment.seams[static_cast<std::size_t>(s)].enabled))
            out.push_back(mesh.allSeamPairs[k]);
    }
    return out;
}

namespace {
/// Points 2D (repère de la pièce posée) le long de ses bords cousus, tels que
/// le maillage les échantillonne : début de chaque bord, puis la fin du dernier.
std::vector<glm::vec2> attachedSamples(const Garment& g, const Attachment& a, const std::map<std::pair<int, int>, int>& segments)
{
    std::vector<glm::vec2> out;
    const auto& pts = g.panels[static_cast<std::size_t>(a.patch)].points;
    const int n = static_cast<int>(pts.size());
    const auto edges = g.attachedEdges(a);
    for (std::size_t i = 0; i < edges.size(); ++i) {
        const int e = edges[i];
        const glm::vec2 p0 = pts[static_cast<std::size_t>(e)], p1 = pts[static_cast<std::size_t>((e + 1) % n)];
        const auto it = segments.find({a.patch, e});
        const int segs = it == segments.end() ? 1 : it->second;
        for (int k = 0; k < segs; ++k) out.push_back(p0 + (p1 - p0) * (static_cast<float>(k) / segs));
        if (i + 1 == edges.size() && static_cast<int>(edges.size()) < n) out.push_back(p1);   // chaîne ouverte : dernier point
    }
    return out;
}

glm::vec2 patchCenter(const Panel& p)
{
    glm::vec2 lo(1e9f), hi(-1e9f);
    for (const auto& q : p.points) {
        lo = glm::min(lo, q);
        hi = glm::max(hi, q);
    }
    return (lo + hi) * 0.5f;
}
} // namespace

bool buildGarmentMesh(const Garment& garment, const MeshingOptions& options, GarmentMesh& out, std::string& error)
{
    out = {};
    // Pièces désactivées en 3D : on maille le vêtement sans elles, puis on remet les index d'origine.
    if (std::any_of(garment.panels.begin(), garment.panels.end(), [](const Panel& p) { return !p.active3D; })) {
        Garment active = garment;
        std::vector<int> panelMap, seamMap;
        for (int p = 0; p < static_cast<int>(garment.panels.size()); ++p)
            if (garment.panels[static_cast<std::size_t>(p)].active3D) panelMap.push_back(p);
        for (int s = 0; s < static_cast<int>(garment.seams.size()); ++s) {
            const auto& sm = garment.seams[static_cast<std::size_t>(s)];
            if (garment.panels[static_cast<std::size_t>(sm.a.panel)].active3D && garment.panels[static_cast<std::size_t>(sm.b.panel)].active3D)
                seamMap.push_back(s);
        }
        for (int p = static_cast<int>(garment.panels.size()) - 1; p >= 0; --p)
            if (!garment.panels[static_cast<std::size_t>(p)].active3D) active.removePanel(p);
        for (auto& p : active.panels) p.active3D = true;
        if (active.panels.empty()) {
            error = "Toutes les pièces sont désactivées en 3D.";
            return false;
        }
        if (!buildGarmentMesh(active, options, out, error)) return false;
        for (auto& p : out.panelOfVertex)
            if (p >= 0 && p < static_cast<int>(panelMap.size())) p = panelMap[static_cast<std::size_t>(p)];
        for (auto& s : out.seamOfPair)
            if (s >= 0 && s < static_cast<int>(seamMap.size())) s = seamMap[static_cast<std::size_t>(s)];
        std::vector<std::vector<glm::vec2>> pts(garment.panels.size());
        for (std::size_t k = 0; k < panelMap.size() && k < out.panelPoints2D.size(); ++k)
            pts[static_cast<std::size_t>(panelMap[k])] = out.panelPoints2D[k];
        out.panelPoints2D = std::move(pts);
        return true;
    }
    const float h0 = std::max(options.spacing, 0.005f);
    // Espacement des particules d'une pièce : le sien s'il est donné, sinon celui du vêtement.
    auto spacingOf = [&](std::size_t p) {
        const float d = garment.panels[p].particleDistance;
        return d > 0.0f ? std::max(d, 0.004f) : h0;
    };

    // --- 1. Nombre de segments par bord (égalisé sur les coutures) ----------
    std::map<std::pair<int, int>, int> segments;
    for (std::size_t p = 0; p < garment.panels.size(); ++p) {
        const int n = static_cast<int>(garment.panels[p].points.size());
        if (n < 3) {
            error = "La pièce « " + garment.panels[p].name + " » a moins de 3 points.";
            return false;
        }
        for (int e = 0; e < n; ++e) {
            const float len = garment.edgeLength({static_cast<int>(p), e});
            segments[{static_cast<int>(p), e}] = std::max(1, static_cast<int>(std::lround(len / spacingOf(p))));
        }
    }
    // Couture : les deux côtés reçoivent le même nombre total de segments, répartis
    // sur les bords de chaque côté au prorata de leur longueur.
    // Un bord peut appartenir à plusieurs coutures (patte cousue sur une couture de milieu devant) : on
    // recommence tant qu'un nombre change, chaque couture gardant au moins les points déjà donnés à ses côtés.
    for (int pass = 0; pass < 6; ++pass) {
    bool changed = false;
    for (const auto& s : garment.seams) {
        const auto A = garment.seamEdges(s, 0), B = garment.seamEdges(s, 1);
        if (A.empty() || B.empty()) continue;
        const float len = std::max(garment.seamLength(s, 0), garment.seamLength(s, 1));
        const float h = std::min(spacingOf(static_cast<std::size_t>(A.front().panel)), spacingOf(static_cast<std::size_t>(B.front().panel)));
        int m = std::max({static_cast<int>(std::lround(len / h)), static_cast<int>(A.size()), static_cast<int>(B.size()), 1});
        if (pass > 0) {
            int sa = 0, sb = 0;
            for (const auto& e : A) sa += segments[{e.panel, e.edge}];
            for (const auto& e : B) sb += segments[{e.panel, e.edge}];
            if (sa == sb && sa >= m) continue;   // déjà d'accord
            m = std::max({m, sa, sb});
        }
        for (const auto* chain : {&A, &B}) {
            float total = 0.0f;
            for (const auto& e : *chain) total += garment.edgeLength(e);
            std::vector<int> segs(chain->size(), 1);
            std::vector<std::pair<float, std::size_t>> rest;
            int used = static_cast<int>(chain->size());
            for (std::size_t k = 0; k < chain->size(); ++k) {
                const float share = total > 0.0f ? garment.edgeLength((*chain)[k]) / total * static_cast<float>(m) : 1.0f;
                const int extra = std::max(0, static_cast<int>(std::floor(share)) - 1);
                segs[k] += extra;
                used += extra;
                rest.push_back({share - std::floor(share), k});
            }
            std::sort(rest.begin(), rest.end(), [](const auto& x, const auto& y) { return x.first > y.first; });
            for (std::size_t k = 0; used < m; k = (k + 1) % rest.size(), ++used) ++segs[rest[k].second];
            for (std::size_t k = 0; k < chain->size(); ++k) {
                int& cur = segments[{(*chain)[k].panel, (*chain)[k].edge}];
                if (cur != segs[k]) changed = true;
                cur = segs[k];
            }
        }
    }
    if (!changed) break;
    }

    // Sommets (indices globaux) de chaque bord, du début à la fin.
    std::map<std::pair<int, int>, std::vector<uint32_t>> edgeVertices;

    std::map<std::size_t, std::vector<int>> attachBaseIds;   // attache -> sommets de la pièce de base
    for (std::size_t pi = 0; pi < garment.panels.size(); ++pi) {
        const Panel& panel = garment.panels[pi];
        const int n = static_cast<int>(panel.points.size());
        const float h = spacingOf(pi);
        std::vector<CDT::V2d<double>> verts;
        std::vector<CDT::Edge> cons;
        std::vector<glm::vec2> local;

        // --- 2a. Contour échantillonné ------------------------------------
        std::vector<int> cornerIndex(static_cast<std::size_t>(n));
        for (int e = 0; e < n; ++e) {
            cornerIndex[static_cast<std::size_t>(e)] = static_cast<int>(local.size());
            const glm::vec2 a = panel.points[static_cast<std::size_t>(e)];
            const glm::vec2 b = panel.points[static_cast<std::size_t>((e + 1) % n)];
            const int segs = segments[{static_cast<int>(pi), e}];
            for (int k = 0; k < segs; ++k) local.push_back(a + (b - a) * (static_cast<float>(k) / segs));
        }
        const auto boundaryCount = static_cast<uint32_t>(local.size());
        for (uint32_t i = 0; i < boundaryCount; ++i) cons.emplace_back(i, (i + 1) % boundaryCount);

        // --- 2a'. Pièces posées sur celle-ci : leur contour devient des sommets
        //          intérieurs, auxquels leurs bords seront cousus.
        std::vector<std::pair<std::size_t, glm::vec2>> attachPts;   // (attache, point) ; ids réservés ensuite
        std::vector<std::vector<int>> attachSlots;                    // par attache : index dans attachPts ou -1
        std::vector<std::size_t> attachHere;
        for (std::size_t ai = 0; ai < garment.attachments.size(); ++ai) {
            const Attachment& at = garment.attachments[ai];
            if (at.base != static_cast<int>(pi) || at.patch < 0 || at.patch >= static_cast<int>(garment.panels.size()) ||
                at.patch == at.base)
                continue;
            attachHere.push_back(ai);
            attachSlots.emplace_back();
            const glm::vec2 shift = at.at - patchCenter(garment.panels[static_cast<std::size_t>(at.patch)]);
            for (const auto& q0 : attachedSamples(garment, at, segments)) {
                const glm::vec2 q = q0 + shift;
                float dmin = 1e9f;
                for (int e = 0; e < n; ++e)
                    dmin = std::min(dmin, distanceToSegment(q, panel.points[static_cast<std::size_t>(e)],
                                                            panel.points[static_cast<std::size_t>((e + 1) % n)]));
                bool ok = pointInPolygon(panel.points, q) && dmin > 0.3f * h;
                for (const auto& [_, other] : attachPts) ok = ok && glm::distance(other, q) > 0.25f * h;
                attachSlots.back().push_back(ok ? static_cast<int>(attachPts.size()) : -1);
                if (ok) attachPts.push_back({ai, q});
            }
        }

        // --- 2b. Points intérieurs (grille décalée d'une demi-maille) -------
        glm::vec2 lo(1e9f), hi(-1e9f);
        for (const auto& q : panel.points) {
            lo = glm::min(lo, q);
            hi = glm::max(hi, q);
        }
        // --- 2a''. Lignes de pli : sommets le long de la ligne, arêtes imposées à la triangulation.
        struct FoldSeg {
            glm::vec2 a, b;
            const FoldLine* f;
        };
        std::vector<FoldSeg> foldSegs;
        {
            const glm::vec2 c0 = panelBoxCenter(panel);
            for (const auto& f : panel.folds) {
                glm::vec2 a = c0 + f.a, b = c0 + f.b;
                // Partie de la ligne à l'intérieur de la pièce (le plus long morceau).
                std::vector<float> ts{0.0f, 1.0f};
                for (int e = 0; e < n; ++e) {
                    const glm::vec2 p = panel.points[static_cast<std::size_t>(e)], q = panel.points[static_cast<std::size_t>((e + 1) % n)];
                    const glm::vec2 d = b - a, ed = q - p, w = p - a;
                    const float den = d.x * ed.y - d.y * ed.x;
                    if (std::abs(den) < 1e-12f) continue;
                    const float t = (w.x * ed.y - w.y * ed.x) / den, u = (w.x * d.y - w.y * d.x) / den;
                    if (t > 0.0f && t < 1.0f && u >= 0.0f && u <= 1.0f) ts.push_back(t);
                }
                std::sort(ts.begin(), ts.end());
                float best = 0.0f, t0 = 0.0f, t1 = 0.0f;
                for (std::size_t k = 0; k + 1 < ts.size(); ++k)
                    if (ts[k + 1] - ts[k] > best && pointInPolygon(panel.points, a + (b - a) * (0.5f * (ts[k] + ts[k + 1])))) {
                        best = ts[k + 1] - ts[k];
                        t0 = ts[k];
                        t1 = ts[k + 1];
                    }
                const glm::vec2 na = a + (b - a) * t0, nb = a + (b - a) * t1;
                if (best <= 0.0f || glm::distance(na, nb) < h) continue;
                // Pas de croisement avec un pli déjà gardé.
                bool crosses = false;
                for (const auto& o : foldSegs) {
                    const glm::vec2 d = nb - na, ed = o.b - o.a, w = o.a - na;
                    const float den = d.x * ed.y - d.y * ed.x;
                    if (std::abs(den) < 1e-12f) continue;
                    const float t = (w.x * ed.y - w.y * ed.x) / den, u = (w.x * d.y - w.y * d.x) / den;
                    crosses = crosses || (t > -0.01f && t < 1.01f && u > -0.01f && u < 1.01f);
                }
                if (!crosses) foldSegs.push_back({na, nb, &f});
            }
        }
        auto nearFold = [&](glm::vec2 q, float r) {
            for (const auto& fs : foldSegs)
                if (distanceToSegment(q, fs.a, fs.b) < r) return true;
            return false;
        };

        for (float y = lo.y + h * 0.5f; y < hi.y; y += h) {
            // une ligne sur deux décalée : triangles quasi équilatéraux
            const float shift = (static_cast<int>(std::lround((y - lo.y) / h)) % 2) ? h * 0.5f : 0.0f;
            for (float x = lo.x + h * 0.5f + shift; x < hi.x; x += h) {
                const glm::vec2 q(x, y);
                if (!pointInPolygon(panel.points, q)) continue;
                float dmin = 1e9f;
                for (int e = 0; e < n; ++e)
                    dmin = std::min(dmin, distanceToSegment(q, panel.points[static_cast<std::size_t>(e)],
                                                            panel.points[static_cast<std::size_t>((e + 1) % n)]));
                bool farFromAttach = true;
                for (const auto& ap : attachPts) farFromAttach = farFromAttach && glm::distance(ap.second, q) > 0.5f * h;
                if (dmin > 0.6f * h && farFromAttach && !nearFold(q, 0.5f * h)) local.push_back(q);
            }
        }
        // Sommets des plis : une extrémité près du bord prend le sommet du contour le plus proche.
        struct LocalFold {
            std::vector<int> ids;
            const FoldLine* f;
        };
        std::vector<LocalFold> localFolds;
        for (const auto& fs : foldSegs) {
            LocalFold lf{{}, fs.f};
            const int k = std::max(1, static_cast<int>(std::lround(glm::distance(fs.a, fs.b) / h)));
            for (int j = 0; j <= k; ++j) {
                const glm::vec2 q = fs.a + (fs.b - fs.a) * (static_cast<float>(j) / static_cast<float>(k));
                float dmin = 1e9f;
                for (int e = 0; e < n; ++e)
                    dmin = std::min(dmin, distanceToSegment(q, panel.points[static_cast<std::size_t>(e)],
                                                            panel.points[static_cast<std::size_t>((e + 1) % n)]));
                if (dmin < 0.6f * h) {
                    if (j != 0 && j != k) continue;   // sommet intermédiaire trop près du bord : sauté
                    int bestV = 0;
                    float bd = 1e9f;
                    for (uint32_t v = 0; v < boundaryCount; ++v)
                        if (const float d = glm::distance(local[v], q); d < bd) {
                            bd = d;
                            bestV = static_cast<int>(v);
                        }
                    // Le sommet du contour (hors coin) est glissé sur l'extrémité du pli.
                    if (dmin < 1e-4f && std::find(cornerIndex.begin(), cornerIndex.end(), bestV) == cornerIndex.end())
                        local[static_cast<std::size_t>(bestV)] = q;
                    lf.ids.push_back(bestV);
                } else {
                    lf.ids.push_back(static_cast<int>(local.size()));
                    local.push_back(q);
                }
            }
            lf.ids.erase(std::unique(lf.ids.begin(), lf.ids.end()), lf.ids.end());
            if (lf.ids.size() < 2) continue;
            for (std::size_t j = 0; j + 1 < lf.ids.size(); ++j)
                cons.emplace_back(static_cast<CDT::VertInd>(lf.ids[j]), static_cast<CDT::VertInd>(lf.ids[j + 1]));
            localFolds.push_back(std::move(lf));
        }

        const std::size_t attachStart = local.size();
        for (const auto& ap : attachPts) local.push_back(ap.second);
        for (const auto& q : local) verts.push_back({q.x, q.y});

        // --- 3. Delaunay contrainte -----------------------------------------
        CDT::Triangulation<double> cdt(CDT::VertexInsertionOrder::Auto);
        try {
            cdt.insertVertices(verts);
            cdt.insertEdges(cons);
            cdt.eraseOuterTriangles();
        } catch (const std::exception& ex) {
            error = "Triangulation impossible pour « " + panel.name + " » : " + ex.what() +
                    " (le contour se croise-t-il ?)";
            return false;
        }
        if (cdt.vertices.size() != verts.size()) {
            error = "Contour invalide pour « " + panel.name + " » (bords qui se croisent).";
            return false;
        }

        // --- 4. Placement 3D --------------------------------------------------
        const auto base = static_cast<uint32_t>(out.mesh.positions.size());
        for (std::size_t k = 0; k < attachHere.size(); ++k) {
            auto& ids = attachBaseIds[attachHere[k]];
            for (const int slot : attachSlots[k])
                ids.push_back(slot < 0 ? -1 : static_cast<int>(base + attachStart + static_cast<std::size_t>(slot)));
        }
        for (const auto& lf : localFolds)
            for (std::size_t j = 0; j + 1 < lf.ids.size(); ++j)
                out.foldEdges.push_back({base + static_cast<uint32_t>(lf.ids[j]), base + static_cast<uint32_t>(lf.ids[j + 1]), lf.f->angleDeg,
                                         lf.f->strength});
        for (const auto& q : local) {
            out.mesh.positions.push_back(placeGarmentPoint(garment, static_cast<int>(pi), q, options));
            out.mesh.uvs.push_back(q);
            out.panelOfVertex.push_back(static_cast<int>(pi));
        }
        // Orientation : normale vers l'extérieur du vêtement = sens trigonométrique en 2D.
        const bool ccwPolygon = signedArea(panel.points) > 0.0f;
        for (const auto& t : cdt.triangles) {
            uint32_t a = t.vertices[0], b = t.vertices[1], c = t.vertices[2];
            const glm::vec2 pa = local[a], pb = local[b], pc = local[c];
            const float cross = (pb.x - pa.x) * (pc.y - pa.y) - (pb.y - pa.y) * (pc.x - pa.x);
            if (cross < 0.0f) std::swap(b, c);
            (void)ccwPolygon;
            out.mesh.indices.insert(out.mesh.indices.end(), {base + a, base + b, base + c});
        }

        // Sommets de chaque bord (y compris les deux coins)
        for (int e = 0; e < n; ++e) {
            const int segs = segments[{static_cast<int>(pi), e}];
            std::vector<uint32_t> ids;
            for (int k = 0; k <= segs; ++k)
                ids.push_back(base + static_cast<uint32_t>((cornerIndex[static_cast<std::size_t>(e)] + k) % boundaryCount));
            edgeVertices[{static_cast<int>(pi), e}] = std::move(ids);
        }
        out.panelPoints2D.push_back(panel.points);
    }

    // --- 5. Coutures ----------------------------------------------------------
    for (std::size_t si = 0; si < garment.seams.size(); ++si) {
        const auto& s = garment.seams[si];
        std::vector<uint32_t> A, B;
        for (int side = 0; side < 2; ++side) {
            auto& ids = side == 0 ? A : B;
            for (const auto& e : garment.seamEdges(s, side)) {
                const auto& ev = edgeVertices[{e.panel, e.edge}];
                ids.insert(ids.end(), ev.begin() + (ids.empty() ? 0 : 1), ev.end());
            }
        }
        if (A.size() != B.size() || A.empty()) continue;
        const std::size_t m = A.size();
        float direct = 0.0f, reversed = 0.0f;
        for (std::size_t k = 0; k < m; ++k) {
            direct += glm::distance(out.mesh.positions[A[k]], out.mesh.positions[B[k]]);
            reversed += glm::distance(out.mesh.positions[A[k]], out.mesh.positions[B[m - 1 - k]]);
        }
        bool reverse = s.order == 0 ? reversed < direct : s.order == 2;
        if (s.flipped) reverse = !reverse;
        for (std::size_t k = 0; k < m; ++k) {
            const uint32_t a = A[k], b = B[reverse ? m - 1 - k : k];
            if (a == b) continue;
            out.allSeamPairs.push_back({a, b});
            out.seamOfPair.push_back(static_cast<int>(si));
            if (s.enabled) out.seamPairs.push_back({a, b});
        }
    }

    // --- 6. Pièces posées : bords de la pièce <-> sommets intérieurs de la base
    for (const auto& [ai, baseIds] : attachBaseIds) {
        const Attachment& at = garment.attachments[ai];
        std::vector<uint32_t> patchIds;
        for (const int e : garment.attachedEdges(at)) {
            const auto& ev = edgeVertices[{at.patch, e}];
            patchIds.insert(patchIds.end(), ev.begin() + (patchIds.empty() ? 0 : 1), ev.end());
        }
        // Chaîne fermée : le dernier sommet répète le premier.
        if (static_cast<int>(garment.attachedEdges(at).size()) == static_cast<int>(garment.panels[static_cast<std::size_t>(at.patch)].points.size()) &&
            !patchIds.empty())
            patchIds.pop_back();
        for (std::size_t k = 0; k < patchIds.size() && k < baseIds.size(); ++k) {
            if (baseIds[k] < 0) continue;
            out.allSeamPairs.push_back({patchIds[k], static_cast<uint32_t>(baseIds[k])});
            out.seamOfPair.push_back(-1);
            out.seamPairs.push_back({patchIds[k], static_cast<uint32_t>(baseIds[k])});
        }
    }

    geometry::buildTopology(out.mesh);
    if (out.mesh.indices.empty()) {
        error = "Aucune pièce à simuler.";
        return false;
    }
    return true;
}

} // namespace drapeo::garment
