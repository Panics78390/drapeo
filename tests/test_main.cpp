// =============================================================================
//  Tests unitaires du cœur (geometry + simulation), sans dépendance graphique.
//  Mini-framework volontairement minimal pour ne rien télécharger de plus ;
//  à remplacer par doctest ou Catch2 quand la base de tests grandira.
// =============================================================================
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <random>
#include <string>
#include <vector>

#include "geometry/ClothMesh.h"
#include "geometry/TriangleBvh.h"
#include "io/MeshImport.h"
#include "io/PatternImport.h"
#include "garment/GarmentMesher.h"
#include "geometry/Mannequin.h"
#include "simulation/Materials.h"
#include "simulation/SpatialHash.h"
#include "simulation/XpbdSolver.h"

using namespace drapeo;

namespace {
int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        ++g_checks;                                                                    \
        if (!(cond)) {                                                                 \
            ++g_failures;                                                              \
            std::printf("  ÉCHEC %s:%d : %s\n", __FILE__, __LINE__, #cond);            \
        }                                                                              \
    } while (0)

void run(const char* name, const std::function<void()>& test)
{
    const int before = g_failures;
    test();
    std::printf("[%s] %s\n", g_failures == before ? " OK " : "FAIL", name);
}

bool allFinite(const std::vector<glm::vec3>& v)
{
    for (const auto& p : v)
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return false;
    return true;
}
} // namespace

int main()
{
    run("Grille : nombre de sommets, triangles, arêtes et paires de flexion", [] {
        const int nx = 7, ny = 5;
        const auto m = geometry::makeRectangle(1.0f, 1.0f, nx, ny, {0, 0, 0});
        CHECK(m.vertexCount() == static_cast<std::size_t>((nx + 1) * (ny + 1)));
        CHECK(m.triangleCount() == static_cast<std::size_t>(2 * nx * ny));
        const std::size_t expectedEdges = nx * (ny + 1) + ny * (nx + 1) + nx * ny;  // h + v + diagonales
        CHECK(m.edges.size() == expectedEdges);
        const std::size_t boundary = 2 * (nx + ny);
        CHECK(m.bendPairs.size() == expectedEdges - boundary);  // une paire par arête intérieure
    });

    run("Grille : normales orientées vers +Y", [] {
        const auto m = geometry::makeRectangle(1.0f, 1.0f, 4, 4, {0, 1, 0});
        std::vector<glm::vec3> normals;
        geometry::computeVertexNormals(m.positions, m.indices, normals);
        bool ok = true;
        for (const auto& n : normals) ok = ok && n.y > 0.99f;
        CHECK(ok);
    });

    run("Hachage spatial : mêmes voisins que la force brute", [] {
        std::mt19937 rng(42);
        std::uniform_real_distribution<float> U(-0.5f, 0.5f);
        std::vector<glm::vec3> pts(2000);
        for (auto& p : pts) p = {U(rng), U(rng), U(rng)};
        const float r = 0.05f;
        simulation::SpatialHash hash(r, pts.size());
        hash.build(pts);
        bool same = true;
        std::vector<uint32_t> cand;
        for (std::size_t i = 0; i < pts.size(); i += 37) {
            cand.clear();
            hash.query(pts[i], r, cand);
            for (std::size_t j = 0; j < pts.size(); ++j) {
                if (glm::distance(pts[i], pts[j]) < r) {
                    const bool found = std::find(cand.begin(), cand.end(), static_cast<uint32_t>(j)) != cand.end();
                    same = same && found;
                }
            }
        }
        CHECK(same);
    });

    run("Chute libre : un tissu sans obstacle suit la gravité", [] {
        // Départ à 2 m : près de l'origine, la précision float suffit (à 10 m, (x - x_prev)/dt
        // avec dt ≈ 1 ms perd des chiffres significatifs, d'où une dérive de quelques cm).
        auto mesh = geometry::makeRectangle(0.5f, 0.5f, 8, 8, {0, 2, 0});
        simulation::XpbdSolver solver;
        solver.build(mesh, simulation::fabricPresets()[0]);
        solver.spheres().clear();
        solver.settings().groundEnabled = false;
        auto mat = simulation::fabricPresets()[0];
        mat.damping = 0.0f;
        solver.setMaterial(mat);
        const float T = 0.5f;
        for (int f = 0; f < 30; ++f) solver.step(T / 30.0f);
        const float expected = 2.0f - 0.5f * 9.81f * T * T;  // y = y0 - g t²/2
        std::printf("    y = %.4f (attendu %.4f)\n", solver.positions()[0].y, expected);
        CHECK(std::abs(solver.positions()[0].y - expected) < 0.02f);
    });

    run("Tissu sur sphère : aucune pénétration, pas de NaN, étirement faible", [] {
        auto mesh = geometry::makeRectangle(1.5f, 1.5f, 40, 40, {0, 1.4f, 0});
        simulation::XpbdSolver solver;
        const auto& cotton = simulation::fabricPresets()[0];
        solver.build(mesh, cotton);
        solver.spheres() = {{{0.0f, 0.6f, 0.0f}, 0.5f}};
        for (int f = 0; f < 180; ++f) solver.step(1.0f / 60.0f);  // 3 secondes

        CHECK(allFinite(solver.positions()));
        float minDist = 1e9f, minY = 1e9f;
        for (const auto& p : solver.positions()) {
            minDist = std::min(minDist, glm::distance(p, glm::vec3(0, 0.6f, 0)));
            minY = std::min(minY, p.y);
        }
        std::printf("    distance min au centre = %.4f m (rayon 0.5), y min = %.4f, étirement moyen = %.2f %%\n",
                    minDist, minY, 100.0f * solver.averageStretchError());
        CHECK(minDist > 0.5f + cotton.thickness * 0.5f);
        CHECK(minY > 0.0f);
        CHECK(solver.averageStretchError() < 0.05f);
    });

    run("Énergie : un tissu épinglé ne remonte jamais au-dessus de son point de départ", [] {
        auto mesh = geometry::makeRectangle(1.6f, 1.6f, 48, 48, {0, 1.5f, 0});
        simulation::XpbdSolver solver;
        auto mat = simulation::fabricPresets()[0];
        mat.damping = 0.0f;  // seul l'amortissement interne reste
        solver.build(mesh, mat);
        solver.spheres().clear();
        solver.settings().groundEnabled = false;
        solver.settings().selfCollision = false;
        solver.setPinned(0, true);
        solver.setPinned(48, true);
        float maxCentroid = -1e9f;
        for (int f = 0; f < 240; ++f) {
            solver.step(1.0f / 60.0f);
            glm::vec3 c(0.0f);
            for (const auto& p : solver.positions()) c += p;
            c /= static_cast<float>(solver.positions().size());
            if (f > 20) maxCentroid = std::max(maxCentroid, c.y);
        }
        std::printf("    centre de masse max = %.3f m (départ 1.500 m)\n", maxCentroid);
        CHECK(maxCentroid < 1.5f);
    });

    run("Épingles : les coins épinglés ne bougent pas", [] {
        auto mesh = geometry::makeRectangle(1.0f, 1.0f, 10, 10, {0, 2, 0});
        simulation::XpbdSolver solver;
        solver.build(mesh, simulation::fabricPresets()[1]);
        solver.setPinned(0, true);
        solver.setPinned(10, true);
        const glm::vec3 p0 = solver.positions()[0];
        for (int f = 0; f < 60; ++f) solver.step(1.0f / 60.0f);
        CHECK(glm::distance(solver.positions()[0], p0) < 1e-6f);
        CHECK(solver.positions()[60].y < 2.0f);  // le reste tombe
    });

    run("Auto-collision : deux couches superposées ne se traversent pas", [] {
        // Deux carrés l'un au-dessus de l'autre qui tombent sur le sol.
        auto a = geometry::makeRectangle(0.4f, 0.4f, 12, 12, {0, 0.30f, 0});
        auto b = geometry::makeRectangle(0.4f, 0.4f, 12, 12, {0.0f, 0.50f, 0.0f});
        const auto offset = static_cast<uint32_t>(a.positions.size());
        a.positions.insert(a.positions.end(), b.positions.begin(), b.positions.end());
        a.uvs.insert(a.uvs.end(), b.uvs.begin(), b.uvs.end());
        for (auto idx : b.indices) a.indices.push_back(idx + offset);
        geometry::buildTopology(a);

        simulation::XpbdSolver solver;
        solver.build(a, simulation::fabricPresets()[0]);
        solver.spheres().clear();
        for (int f = 0; f < 120; ++f) solver.step(1.0f / 60.0f);
        // La couche du haut doit rester au-dessus de la couche du bas au centre.
        const std::size_t center = 6 * 13 + 6;
        const float yLow = solver.positions()[center].y;
        const float yHigh = solver.positions()[offset + center].y;
        std::printf("    centre bas y = %.4f, centre haut y = %.4f\n", yLow, yHigh);
        CHECK(yHigh > yLow + 0.005f);
    });

    run("BVH : point le plus proche identique à la force brute", [] {
        std::vector<glm::vec3> pos;
        std::vector<uint32_t> idx;
        geometry::makeUvSphere(16, 32, pos, idx);
        geometry::TriangleBvh bvh;
        bvh.build(pos, idx);
        std::mt19937 rng(7);
        std::uniform_real_distribution<float> U(-1.5f, 1.5f);
        bool same = true;
        for (int k = 0; k < 300; ++k) {
            const glm::vec3 p(U(rng), U(rng), U(rng));
            float best = 1e9f;
            for (std::size_t t = 0; t < idx.size(); t += 3)
                best = std::min(best, glm::distance(p, geometry::closestPointOnTriangle(p, pos[idx[t]], pos[idx[t + 1]], pos[idx[t + 2]])));
            geometry::ClosestHit hit;
            const bool found = bvh.closestPoint(p, 10.0f, hit);
            same = same && found && std::abs(hit.distance - best) < 1e-5f;
        }
        CHECK(same);
    });

    run("Import OBJ : mannequin en cm converti et posé au sol", [] {
        io::ImportedMesh mesh;
        std::string error;
        const bool ok = io::importMesh(DRAPEO_ASSETS_DIR "/mannequin.obj", mesh, error);
        if (!ok) std::printf("    erreur : %s\n", error.c_str());
        CHECK(ok);
        if (!ok) return;
        std::printf("    %zu sommets, %zu triangles, hauteur lue %.2f\n", mesh.positions.size(), mesh.indices.size() / 3,
                    static_cast<double>(mesh.sourceHeight));
        auto pos = mesh.positions;
        const float h = io::fitToGround(pos, {});
        float lo = 1e9f, hi = -1e9f;
        for (const auto& p : pos) { lo = std::min(lo, p.y); hi = std::max(hi, p.y); }
        CHECK(std::abs(h - 1.75f) < 1e-3f);          // 175 « unités » : pas des mètres -> 1,75 m
        CHECK(std::abs(lo) < 1e-4f);
        CHECK(std::abs(hi - 1.75f) < 1e-3f);
    });

    run("Import : un fichier absent renvoie une erreur lisible", [] {
        io::ImportedMesh mesh;
        std::string error;
        CHECK(!io::importMesh("introuvable.fbx", mesh, error));
        CHECK(!error.empty());
    });

    run("Tissu sur avatar : pas de pénétration dans le corps", [] {
        io::ImportedMesh mesh;
        std::string error;
        if (!io::importMesh(DRAPEO_ASSETS_DIR "/mannequin.obj", mesh, error)) { CHECK(false); return; }
        io::fitToGround(mesh.positions, {});
        geometry::TriangleBvh bvh;
        bvh.build(mesh.positions, mesh.indices);
        auto cloth = geometry::makeRectangle(1.4f, 1.4f, 48, 48, {0, 1.87f, 0});
        simulation::XpbdSolver solver;
        solver.build(cloth, simulation::fabricPresets()[0]);
        solver.spheres().clear();
        solver.setMeshCollider(&bvh);
        for (int f = 0; f < 150; ++f) solver.step(1.0f / 60.0f);
        CHECK(allFinite(solver.positions()));
        // Une particule à l'intérieur du corps a son point le plus proche « devant » elle.
        int inside = 0;
        for (const auto& p : solver.positions()) {
            geometry::ClosestHit hit;
            if (bvh.closestPoint(p, 0.05f, hit) && glm::dot(p - hit.point, hit.normal) < -0.002f) ++inside;
        }
        std::printf("    %d particules sous la surface sur %zu, %zu en contact\n", inside, solver.positions().size(),
                    solver.meshContacts());
        CHECK(inside < static_cast<int>(solver.positions().size() / 100));  // < 1 %
        CHECK(solver.meshContacts() > 20);  // le tissu repose bien sur le corps
    });

    auto drape = [](const garment::Garment& g, const char* label, float minExpectedY) {
        std::vector<glm::vec3> body;
        std::vector<uint32_t> bodyIdx;
        geometry::makeMannequin(body, bodyIdx);
        geometry::TriangleBvh bvh;
        bvh.build(body, bodyIdx);
        garment::GarmentMesh gm;
        std::string error;
        const bool ok = garment::buildGarmentMesh(g, {}, gm, error);
        if (!ok) std::printf("    erreur : %s\n", error.c_str());
        CHECK(ok);
        if (!ok) return;
        simulation::XpbdSolver solver;
        solver.build(gm.mesh, simulation::fabricPresets()[0]);
        solver.spheres().clear();
        solver.setMeshCollider(&bvh);
        solver.setSeams(gm.seamPairs, 1.0f);
        for (int f = 0; f < 180; ++f) solver.step(1.0f / 60.0f);
        CHECK(allFinite(solver.positions()));
        float gap = 0.0f;
        for (const auto& p : gm.seamPairs) gap = std::max(gap, glm::distance(solver.positions()[p[0]], solver.positions()[p[1]]));
        float minY = 1e9f;
        for (const auto& p : solver.positions()) minY = std::min(minY, p.y);
        int inside = 0;
        for (const auto& p : solver.positions()) {
            geometry::ClosestHit hit;
            if (bvh.closestPoint(p, 0.05f, hit) && glm::dot(p - hit.point, hit.normal) < -0.002f) ++inside;
        }
        std::printf("    %s : %zu particules, %zu triangles, %zu paires cousues, écart max %.1f mm, y min %.2f m, %d sous la peau\n",
                    label, gm.mesh.vertexCount(), gm.mesh.triangleCount(), gm.seamPairs.size(),
                    static_cast<double>(gap * 1000.0f), static_cast<double>(minY), inside);
        CHECK(gap < 0.01f);                 // coutures fermées à 1 cm près
        CHECK(minY > minExpectedY);         // le vêtement tient sur le corps
        CHECK(inside < static_cast<int>(gm.mesh.vertexCount() / 50));
    };

    run("Vêtement : débardeur cousu et drapé sur le mannequin", [&] { drape(garment::makeTankTop(), "débardeur", 0.85f); });
    run("Vêtement : jupe cousue et drapée sur le mannequin", [&] { drape(garment::makeSkirt(), "jupe", 0.4f); });

    run("Maillage de patron : coutures appariées et bords respectés", [] {
        garment::GarmentMesh gm;
        std::string error;
        CHECK(garment::buildGarmentMesh(garment::makeTankTop(), {}, gm, error));
        CHECK(!gm.seamPairs.empty());
        CHECK(gm.mesh.bendPairs.size() > 0);
        // Toutes les UV sont à l'intérieur (ou sur le bord) de leur pièce.
        bool inside = true;
        const auto g = garment::makeTankTop();
        for (std::size_t v = 0; v < gm.mesh.vertexCount(); ++v) {
            const auto& poly = g.panels[static_cast<std::size_t>(gm.panelOfVertex[v])].points;
            float d = 1e9f;
            for (std::size_t e = 0; e < poly.size(); ++e) d = std::min(d, garment::distanceToSegment(gm.mesh.uvs[v], poly[e], poly[(e + 1) % poly.size()]));
            inside = inside && (garment::pointInPolygon(poly, gm.mesh.uvs[v]) || d < 1e-4f);
        }
        CHECK(inside);
    });

    run("Import SVG et DXF : contours fermés en mètres", [] {
        std::vector<std::vector<glm::vec2>> out;
        std::string error;
        const std::string svg = R"(<svg width="300mm" height="200mm" viewBox="0 0 300 200">
            <rect x="10" y="10" width="100" height="50"/>
            <path d="M 150 10 L 250 10 L 250 110 C 220 140 180 140 150 110 Z"/></svg>)";
        CHECK(garment::signedArea({{0, 0}, {1, 0}, {1, 1}}) > 0.0f);
        CHECK(io::parseSvg(svg, out, error));
        CHECK(out.size() == 2);
        if (out.size() == 2) {
            glm::vec2 lo(1e9f), hi(-1e9f);
            for (const auto& p : out[0]) { lo = glm::min(lo, p); hi = glm::max(hi, p); }
            CHECK(std::abs((hi.x - lo.x) - 0.100f) < 1e-4f);   // 100 mm
            CHECK(std::abs((hi.y - lo.y) - 0.050f) < 1e-4f);
        }
        out.clear();
        const std::string dxf = "0\nSECTION\n2\nHEADER\n9\n$INSUNITS\n70\n5\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n"
                                "0\nLWPOLYLINE\n90\n4\n70\n1\n10\n0\n20\n0\n10\n40\n20\n0\n10\n40\n20\n60\n10\n0\n20\n60\n"
                                "0\nLINE\n10\n100\n20\n0\n11\n130\n21\n0\n0\nLINE\n10\n130\n20\n0\n11\n115\n21\n30\n"
                                "0\nLINE\n10\n115\n20\n30\n11\n100\n21\n0\n0\nENDSEC\n0\nEOF\n";
        CHECK(io::parseDxf(dxf, out, error));
        CHECK(out.size() == 2);
        if (!out.empty()) CHECK(std::abs(glm::distance(out[0][0], out[0][1]) - 0.40f) < 1e-4f);   // 40 cm
    });

    std::printf("\n%d vérifications, %d échec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
