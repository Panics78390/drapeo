#include "garment/GarmentMesher.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>

#include <CDT.h>

namespace drapeo::garment {

namespace {
struct PanelFrame {
    float cx = 0.0f, minY = 0.0f;
};

PanelFrame frameOf(const Panel& p)
{
    float lo = 1e9f, hi = -1e9f, minY = 1e9f;
    for (const auto& q : p.points) {
        lo = std::min(lo, q.x);
        hi = std::max(hi, q.x);
        minY = std::min(minY, q.y);
    }
    return {(lo + hi) * 0.5f, minY};
}
} // namespace

glm::vec3 placePoint(const Panel& panel, glm::vec2 p, const MeshingOptions& options)
{
    const PanelFrame f = frameOf(panel);
    const float R = std::max(panel.placement.radius * options.bodyScale, 0.05f);
    const float u = p.x - f.cx;           // abscisse le long du tour du corps
    const float v = p.y - f.minY;         // hauteur dans la pièce
    const float a = glm::radians(panel.placement.angleDeg) + u / R;
    return options.bodyCenter + glm::vec3(R * std::sin(a), panel.placement.height * options.bodyScale + v, R * std::cos(a));
}

bool buildGarmentMesh(const Garment& garment, const MeshingOptions& options, GarmentMesh& out, std::string& error)
{
    out = {};
    const float h = std::max(options.spacing, 0.005f);

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
            segments[{static_cast<int>(p), e}] = std::max(1, static_cast<int>(std::lround(len / h)));
        }
    }
    for (const auto& s : garment.seams) {
        const int m = std::max(segments[{s.a.panel, s.a.edge}], segments[{s.b.panel, s.b.edge}]);
        segments[{s.a.panel, s.a.edge}] = m;
        segments[{s.b.panel, s.b.edge}] = m;
    }

    // Sommets (indices globaux) de chaque bord, du début à la fin.
    std::map<std::pair<int, int>, std::vector<uint32_t>> edgeVertices;

    for (std::size_t pi = 0; pi < garment.panels.size(); ++pi) {
        const Panel& panel = garment.panels[pi];
        const int n = static_cast<int>(panel.points.size());
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

        // --- 2b. Points intérieurs (grille décalée d'une demi-maille) -------
        glm::vec2 lo(1e9f), hi(-1e9f);
        for (const auto& q : panel.points) {
            lo = glm::min(lo, q);
            hi = glm::max(hi, q);
        }
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
                if (dmin > 0.6f * h) local.push_back(q);
            }
        }

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
        for (const auto& q : local) {
            out.mesh.positions.push_back(placePoint(panel, q, options));
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
    for (const auto& s : garment.seams) {
        const auto& A = edgeVertices[{s.a.panel, s.a.edge}];
        const auto& B = edgeVertices[{s.b.panel, s.b.edge}];
        if (A.size() != B.size() || A.empty()) continue;
        const std::size_t m = A.size();
        float direct = 0.0f, reversed = 0.0f;
        for (std::size_t k = 0; k < m; ++k) {
            direct += glm::distance(out.mesh.positions[A[k]], out.mesh.positions[B[k]]);
            reversed += glm::distance(out.mesh.positions[A[k]], out.mesh.positions[B[m - 1 - k]]);
        }
        bool reverse = reversed < direct;
        if (s.flipped) reverse = !reverse;
        for (std::size_t k = 0; k < m; ++k) {
            const uint32_t a = A[k], b = B[reverse ? m - 1 - k : k];
            if (a != b) out.seamPairs.push_back({a, b});
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
