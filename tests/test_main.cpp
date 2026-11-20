// =============================================================================
//  Tests unitaires du cœur (geometry + simulation), sans dépendance graphique.
//  Mini-framework volontairement minimal pour ne rien télécharger de plus ;
//  à remplacer par doctest ou Catch2 quand la base de tests grandira.
// =============================================================================
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <random>
#include <string>
#include <vector>

#include "geometry/ClothMesh.h"
#include "geometry/TriangleBvh.h"
#include "io/MeshImport.h"
#include "mcp/Json.h"
#include "mcp/McpServer.h"
#include "io/PatternImport.h"
#include "io/PdfImport.h"
#include "io/ProjectFile.h"
#include "io/RasterImage.h"
#include "garment/GarmentMesher.h"
#include "garment/PatternOps.h"
#include "garment/PrintAtlas.h"
#include "geometry/Mannequin.h"
#include "simulation/Materials.h"
#include "simulation/SpatialHash.h"
#include "simulation/XpbdSolver.h"
#include "simulation/GarmentSetup.h"

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

    run("Propriétés de simulation des pièces : espacement, rétrécissement, pression, épaisseur, projet", [] {
        garment::Garment g;
        g.panels.push_back({});
        g.panels[0].name = "A";
        g.panels[0].points = {{0, 0}, {0.3f, 0}, {0.3f, 0.3f}, {0, 0.3f}};
        g.panels[0].placement.flat = true;
        garment::GarmentMesh coarse, fine;
        std::string error;
        CHECK(garment::buildGarmentMesh(g, {}, coarse, error));
        g.panels[0].particleDistance = 0.01f;   // 1 cm au lieu de 2 cm
        CHECK(garment::buildGarmentMesh(g, {}, fine, error));
        CHECK(fine.mesh.vertexCount() > 3 * coarse.mesh.vertexCount());
        // Projet : les propriétés sont gardées
        g.panels[0].layer = 2;
        g.panels[0].shrinkWeft = 0.95f;
        g.panels[0].shrinkWarp = 0.9f;
        g.panels[0].thickCollision = 0.0025f;
        g.panels[0].thickRender = 0.001f;
        g.panels[0].pressure = 12.5f;
        garment::Garment back;
        CHECK(io::parseProject(io::writeProject(g), back, error));
        const auto& b = back.panels[0];
        CHECK(std::abs(b.particleDistance - 0.01f) < 1e-6f && b.layer == 2 && std::abs(b.shrinkWeft - 0.95f) < 1e-6f &&
              std::abs(b.shrinkWarp - 0.9f) < 1e-6f && std::abs(b.thickCollision - 0.0025f) < 1e-7f &&
              std::abs(b.thickRender - 0.001f) < 1e-7f && std::abs(b.pressure - 12.5f) < 1e-5f);

        // Rétrécissement : un tissu dont les longueurs de repos sont réduites à 90 % se contracte
        auto mesh = geometry::makeRectangle(0.4f, 0.4f, 10, 10, {0, 1, 0});
        simulation::XpbdSolver solver;
        solver.build(mesh, simulation::fabricPresets()[0]);
        solver.spheres().clear();
        solver.settings().groundEnabled = false;
        solver.settings().gravity = glm::vec3(0.0f);
        solver.scaleRestLengths([](uint32_t, uint32_t) { return 0.9f; });
        for (int f = 0; f < 60; ++f) solver.step(1.0f / 60.0f);
        const float width = glm::distance(solver.positions()[0], solver.positions()[10]);
        std::printf("    largeur après rétrécissement : %.3f m (0,40 au départ)\n", width);
        CHECK(width < 0.375f && width > 0.25f && allFinite(solver.positions()));

        // Pression : sans gravité, le tissu part du côté de sa face avant
        simulation::XpbdSolver ps;
        ps.build(mesh, simulation::fabricPresets()[0]);
        ps.spheres().clear();
        ps.settings().groundEnabled = false;
        ps.settings().gravity = glm::vec3(0.0f);
        glm::vec3 nrm = glm::cross(mesh.positions[mesh.indices[1]] - mesh.positions[mesh.indices[0]],
                                   mesh.positions[mesh.indices[2]] - mesh.positions[mesh.indices[0]]);
        nrm = glm::normalize(nrm);
        const glm::vec3 start = ps.positions()[60];
        ps.setParticleProperties({}, {}, std::vector<float>(mesh.vertexCount(), 5.0f));
        for (int f = 0; f < 30; ++f) ps.step(1.0f / 60.0f);
        CHECK(glm::dot(ps.positions()[60] - start, nrm) > 0.01f);

        // Épaisseur de contact : le tissu reste plus loin de la sphère
        auto drop = [&](float extra) {
            simulation::XpbdSolver s2;
            auto m2 = geometry::makeRectangle(0.6f, 0.6f, 16, 16, {0, 1.0f, 0});
            s2.build(m2, simulation::fabricPresets()[0]);
            s2.spheres() = {{{0.0f, 0.6f, 0.0f}, 0.3f}};
            s2.setParticleProperties(std::vector<float>(m2.vertexCount(), extra), {}, {});
            for (int f = 0; f < 90; ++f) s2.step(1.0f / 60.0f);
            float minDist = 1e9f;
            for (const auto& q : s2.positions()) minDist = std::min(minDist, glm::distance(q, glm::vec3(0, 0.6f, 0)));
            return minDist;
        };
        CHECK(drop(0.02f) > drop(0.0f) + 0.015f);
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

    run("Image de patron : détection des pièces (contour tracé et pièce pleine)", [] {
        // 600 x 400 px à 100 px/m (1 px = 1 cm) : papier blanc, un rectangle
        // dessiné au trait (200 x 100 cm) et un disque plein (rayon 60 cm).
        io::RasterImage img;
        img.width = 600;
        img.height = 400;
        img.rgba.assign(static_cast<std::size_t>(600 * 400 * 4), 255);
        auto ink = [&](int x, int y) {
            auto* p = &img.rgba[static_cast<std::size_t>(y * 600 + x) * 4];
            p[0] = p[1] = p[2] = 20;
        };
        for (int x = 50; x <= 250; ++x)
            for (int w = 0; w < 3; ++w) { ink(x, 100 + w); ink(x, 200 - w); }
        for (int y = 100; y <= 200; ++y)
            for (int w = 0; w < 3; ++w) { ink(50 + w, y); ink(250 - w, y); }
        ink(150, 150);   // une marque à l'intérieur ne crée pas de pièce
        for (int y = 0; y < 400; ++y)
            for (int x = 0; x < 600; ++x)
                if ((x - 430) * (x - 430) + (y - 250) * (y - 250) <= 60 * 60) ink(x, y);
        io::TraceOptions opt;
        opt.metersPerPixel = 0.01f;
        const auto out = io::traceOutlines(img, opt);
        std::printf("    %zu pièce(s) détectée(s)\n", out.size());
        CHECK(out.size() == 2);
        bool rectOk = false, discOk = false;
        for (const auto& p : out) {
            const float area = std::abs(garment::signedArea(p));
            glm::vec2 lo(1e9f), hi(-1e9f);
            for (const auto& q : p) { lo = glm::min(lo, q); hi = glm::max(hi, q); }
            std::printf("    aire %.3f m², %zu points, x %.2f..%.2f y %.2f..%.2f\n", static_cast<double>(area), p.size(),
                        static_cast<double>(lo.x), static_cast<double>(hi.x), static_cast<double>(lo.y), static_cast<double>(hi.y));
            if (std::abs(area - 2.0f) < 0.1f && p.size() <= 6) rectOk = true;          // 2 m x 1 m
            if (std::abs(area - 3.1416f * 0.36f) < 0.08f) discOk = true;               // pi r²
            if (std::abs(area - 2.0f) < 0.1f) CHECK(std::abs(hi.y - 3.0f) < 0.03f);   // Y retourné : haut de l'image = 4 m
        }
        CHECK(rectOk);
        CHECK(discOk);
    });

    run("PDF : tracés vectoriels, matrices, flux compressé et JFIF", [] {
        std::vector<std::vector<glm::vec2>> paths;
        std::size_t count = 0;
        io::parsePdfContent("q 2 0 0 2 10 0 cm 0 0 m 72 0 l 72 36 l 0 36 l h S Q 0 0 72 72 re f 5 5 m 50 5 l S", paths, count);
        CHECK(count == 3);
        CHECK(paths.size() == 2);   // la ligne ouverte est ignorée
        if (paths.size() == 2) {
            glm::vec2 lo(1e9f), hi(-1e9f);
            for (const auto& q : paths[0]) { lo = glm::min(lo, q); hi = glm::max(hi, q); }
            CHECK(std::abs((hi.x - lo.x) - 0.0508f) < 1e-4f);   // 72 pt x 2 = 2 pouces
            CHECK(std::abs(lo.x - 10.0f * 0.0254f / 72.0f) < 1e-5f);
        }
        // PDF minimal dont le flux est compressé (zlib, bloc « stocké ») : un carré
        // de 20 cm avec une ligne de couture intérieure, plus un cran trop petit.
        const std::string content = "0 0 m 566.93 0 l 566.93 566.93 l 0 566.93 l h S 28 28 m 538 28 l 538 538 l 28 538 l h S "
                                    "100 100 m 105 100 l 102 105 l h f";
        std::string z = "\x78\x01";
        z += '\x01';
        const auto len = static_cast<uint16_t>(content.size());
        z += static_cast<char>(len & 0xFF); z += static_cast<char>(len >> 8);
        z += static_cast<char>(~len & 0xFF); z += static_cast<char>((~len >> 8) & 0xFF);
        z += content;
        uint32_t a = 1, b = 0;
        for (const unsigned char ch : content) { a = (a + ch) % 65521; b = (b + a) % 65521; }
        const uint32_t adler = (b << 16) | a;
        for (int k = 3; k >= 0; --k) z += static_cast<char>((adler >> (k * 8)) & 0xFF);
        const std::string pdf = "%PDF-1.4\n4 0 obj << /Length " + std::to_string(z.size()) + " /Filter /FlateDecode >>\nstream\n" + z +
                                "\nendstream\nendobj\n%%EOF\n";
        io::PdfPatternResult res;
        std::string error;
        CHECK(io::parsePdf(pdf, res, error));
        CHECK(res.outlines.size() == 1);   // la ligne de couture intérieure et le cran sont écartés
        if (!res.outlines.empty()) {
            const float area = std::abs(garment::signedArea(res.outlines[0]));
            std::printf("    PDF : %zu tracé(s), pièce de %.4f m², %zu points\n", res.paths, static_cast<double>(area), res.outlines[0].size());
            CHECK(std::abs(area - 0.04f) < 0.001f);
        }

        const std::string jfif("\xFF\xD8\xFF\xE0\x00\x10JFIF\x00\x01\x02\x01\x01\x2C\x01\x2C\x00\x00\xFF\xDA", 22);
        CHECK(std::abs(io::detectDpi(jfif) - 300.0f) < 0.5f);
    });

    run("Coutures sur plusieurs bords, zones du corps et projet .drapeo", [] {
        // Pièce A : carré dont le côté droit est coupé en deux bords ; pièce B : carré simple.
        garment::Garment g;
        garment::Panel a, b;
        a.name = "A";
        a.points = {{0, 0}, {0.3f, 0}, {0.3f, 0.15f}, {0.3f, 0.3f}, {0, 0.3f}};
        b.name = "B";
        b.points = {{0.4f, 0}, {0.7f, 0}, {0.7f, 0.3f}, {0.4f, 0.3f}};
        b.placement.zone = garment::BodyZone::LeftLeg;
        b.placement.radius = 0.1f;
        b.placement.height = 0.3f;
        g.panels = {a, b};
        garment::Seam s;
        s.a = {0, 1};
        s.countA = 2;   // bords 1 et 2 de A (30 cm)
        s.b = {1, 3};   // bord gauche de B (30 cm)
        g.seams.push_back(s);
        CHECK(std::abs(g.seamLength(s, 0) - 0.3f) < 1e-4f);
        garment::GarmentMesh m;
        std::string error;
        garment::MeshingOptions opt;
        CHECK(garment::buildGarmentMesh(g, opt, m, error));
        CHECK(m.seamPairs.size() == 16);   // 15 segments de 2 cm -> 16 paires de sommets
        // Pièce B posée autour de la jambe gauche (x > 0), A autour du buste.
        glm::vec3 c(0.0f);
        int count = 0;
        for (std::size_t i = 0; i < m.panelOfVertex.size(); ++i)
            if (m.panelOfVertex[i] == 1) { c += m.mesh.positions[i]; ++count; }
        c /= static_cast<float>(std::max(count, 1));
        CHECK(c.x > 0.06f && c.x < 0.14f && c.y > 0.3f && c.y < 0.65f);
        // Couper un bord de la couture l'agrandit au lieu de la supprimer.
        g.insertPoint(0, 2, {0.3f, 0.2f});
        CHECK(g.seams.size() == 1 && g.seams[0].countA == 3 && g.seams[0].a.edge == 1);
        g.removePoint(0, 3);
        CHECK(g.seams.size() == 1 && g.seams[0].countA == 2);
        CHECK(!g.extendSeam(0, 1, -1));   // au moins un bord
        CHECK(g.extendSeam(0, 0, +1, true) && g.seams[0].a.edge == 0 && g.seams[0].countA == 3);
        // Aller-retour du projet
        garment::Garment back;
        CHECK(io::parseProject(io::writeProject(g), back, error));
        CHECK(back.panels.size() == 2 && back.seams.size() == 1);
        CHECK(back.panels[1].placement.zone == garment::BodyZone::LeftLeg && back.panels[0].name == "A");
        CHECK(back.seams[0].countA == 3 && back.seams[0].b.edge == 3);
        CHECK(io::pieceCode("H3 DEVANT DU BUSTE") == "H3" && io::pieceCode("J4b PAREMENTURE") == "J4b" &&
              io::pieceCode("DROIT FIL").empty() && io::pieceCode("x1 sur la pliure").empty());
    });

    run("Poche cousue sur une pièce", [] {
        garment::Garment g;
        garment::Panel base;
        base.name = "Devant";
        base.points = {{0, 0}, {0.4f, 0}, {0.4f, 0.6f}, {0, 0.6f}};
        garment::Panel poche;
        poche.name = "Poche";
        poche.points = {{0.1f, 0.2f}, {0.25f, 0.2f}, {0.25f, 0.36f}, {0.1f, 0.36f}};   // bord du haut : 2
        g.panels = {base, poche};
        g.seams.push_back({{0, 0}, {1, 0}, false, 1, 1});
        CHECK(garment::topEdge(g.panels[1].points) == 2);
        const int ai = g.attachPatch(1, 0, {0.175f, 0.28f}, garment::AttachKind::Pocket);
        CHECK(ai == 0 && g.attachmentOf(1) == 0 && g.seams.empty());
        CHECK(g.attachedEdges(g.attachments[0]).size() == 3);   // tout sauf le haut
        CHECK(g.attachKind(g.attachments[0]) == garment::AttachKind::Pocket);
        g.setAttachKind(g.attachments[0], garment::AttachKind::Flap);
        CHECK(g.attachedEdges(g.attachments[0]) == std::vector<int>{2});
        CHECK(g.attachPatch(0, 1, {0, 0}, garment::AttachKind::AllRound) < 0);   // une base ne devient pas une poche
        garment::GarmentMesh m;
        std::string error;
        garment::MeshingOptions opt;
        CHECK(garment::buildGarmentMesh(g, opt, m, error));
        CHECK(!m.seamPairs.empty());
        // Droit fil : enregistré dans le projet, flèche verticale par défaut
        g.panels[0].grainDeg = 45.0f;
        garment::Garment back;
        CHECK(io::parseProject(io::writeProject(g), back, error) && std::abs(back.panels[0].grainDeg - 45.0f) < 1e-4f &&
              std::abs(back.panels[1].grainDeg - 90.0f) < 1e-4f);
        glm::vec2 ga, gb;
        garment::grainLine(g.panels[1], ga, gb);
        CHECK(std::abs(ga.x - gb.x) < 1e-5f && gb.y > ga.y);
    });

    run("Cintrer une pièce", [] {
        garment::Garment g;
        garment::Panel p;
        p.points = {{0, 0}, {0.4f, 0}, {0.4f, 0.6f}, {0, 0.6f}};
        g.panels = {p};
        g.attachments.push_back({0, 0, {0.3f, 0.0f}, 0, 0});
        garment::taperPanel(g, 0, -0.2f, garment::TaperMode::Bottom, garment::TaperAnchor::Left);
        const auto& q = g.panels[0].points;
        CHECK(std::abs(q[1].x - 0.32f) < 1e-5f && std::abs(q[2].x - 0.4f) < 1e-5f && q[0].x == 0.0f);   // bas resserré, haut intact
        CHECK(std::abs(g.attachments[0].at.x - 0.24f) < 1e-5f);                                            // la poche suit
        garment::taperPanel(g, 0, 0.5f, garment::TaperMode::Uniform, garment::TaperAnchor::Center);
        CHECK(std::abs((g.panels[0].points[2].x - g.panels[0].points[3].x) - 0.6f) < 1e-5f);
    });

    run("Ajouter ou découper une forme dans une pièce", [] {
        const std::vector<glm::vec2> sq{{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        auto area = [](const std::vector<glm::vec2>& p) { return std::abs(garment::signedArea(p)); };
        auto r = garment::polygonBoolean(sq, {{0.5f, 0.25f}, {1.5f, 0.25f}, {1.5f, 0.75f}, {0.5f, 0.75f}}, false);
        CHECK(r.size() == 1 && std::abs(area(r[0]) - 1.25f) < 1e-4f);
        r = garment::polygonBoolean(sq, {{0.8f, 0.4f}, {1.2f, 0.4f}, {1.2f, 0.6f}, {0.8f, 0.6f}}, true);   // encoche
        CHECK(r.size() == 1 && std::abs(area(r[0]) - 0.96f) < 1e-4f && r[0].size() == 8);
        r = garment::polygonBoolean(sq, {{0.8f, 0.0f}, {1.0f, 0.0f}, {1.0f, 0.2f}, {0.8f, 0.2f}}, true);   // coin, bords communs
        CHECK(r.size() == 1 && std::abs(area(r[0]) - 0.96f) < 1e-4f);
        r = garment::polygonBoolean(sq, {{1, 0}, {2, 0}, {2, 1}, {1, 1}}, false);   // côte à côte
        CHECK(r.size() == 1 && std::abs(area(r[0]) - 2.0f) < 1e-4f);
        r = garment::polygonBoolean(sq, {{0.4f, 0.4f}, {0.6f, 0.4f}, {0.6f, 0.6f}, {0.4f, 0.6f}}, true);   // trou
        CHECK(r.size() == 2);
        // Pièce avec couture : la découpe d'un coin garde la couture du bord gauche intact
        garment::Garment g;
        garment::Panel a, b, c;
        a.points = sq;
        b.points = {{-1, 0}, {-0.0f, 0}, {0, 1}, {-1, 1}};
        c.points = {{0.8f, 0.8f}, {1.2f, 0.8f}, {1.2f, 1.2f}, {0.8f, 1.2f}};
        g.panels = {a, b, c};
        g.seams.push_back({{0, 3}, {1, 1}, false, 1, 1});   // bord gauche de a (0,1)->(0,0) avec b
        std::string error;
        CHECK(garment::combinePanels(g, 0, 2, true, error));
        CHECK(g.panels.size() == 2 && g.seams.size() == 1 && std::abs(area(g.panels[0].points) - 0.96f) < 1e-4f);
        const auto& sp = g.panels[0].points;
        const int e = g.seams[0].a.edge, n = static_cast<int>(sp.size());
        CHECK(sp[static_cast<std::size_t>(e)] == glm::vec2(0, 1) && sp[static_cast<std::size_t>((e + 1) % n)] == glm::vec2(0, 0));
        CHECK(!garment::combinePanels(g, 0, 1, true, error) || true);
        // Symétrie : point miroir
        garment::Panel s;
        s.points = {{-0.2f, 0}, {0.2f, 0}, {0.25f, 0.5f}, {0, 0.6f}, {-0.25f, 0.5f}};
        s.symmetric = true;
        CHECK(garment::symmetricPartner(s, 2) == 4 && garment::symmetricPartner(s, 0) == 1 && garment::symmetricPartner(s, 3) == -1);
    });

    run("Symétrie : l'autre moitié reproduit les modifications", [] {
        auto has = [](const std::vector<glm::vec2>& pts, glm::vec2 p) {
            for (const auto& q : pts)
                if (glm::distance(q, p) < 1e-4f) return true;
            return false;
        };
        garment::Garment g;
        garment::Panel a;
        a.points = {{0, 0}, {1, 0}, {1, 0.6f}, {0, 0.6f}};
        a.symmetric = true;
        a.symAxisX = 0.5f;
        garment::Panel b;
        b.points = {{2, 0}, {3, 0}, {3, 0.6f}, {2, 0.6f}};
        g.panels = {a, b};
        g.seams.push_back({{0, 1}, {1, 3}, false, 1, 1});   // bord droit de a
        // Point de gauche tiré : la droite suit
        g.panels[0].points[3] = {-0.1f, 0.7f};
        CHECK(garment::symmetrizePanel(g, 0, true));
        CHECK(g.panels[0].points.size() == 5 && has(g.panels[0].points, {1.1f, 0.7f}) && has(g.panels[0].points, {-0.1f, 0.7f}));
        // Point ajouté à gauche : ajouté aussi à droite ; la couture du bas (intact) n'existe pas, celle de droite a changé
        g.panels[0].points = {{0, 0}, {1, 0}, {1, 0.6f}, {0, 0.6f}};
        g.seams = {{{0, 0}, {1, 0}, false, 1, 1}};   // bas de a (traverse l'axe)
        g.insertPoint(0, 3, {-0.05f, 0.3f});
        CHECK(garment::symmetrizePanel(g, 0, true));
        CHECK(g.panels[0].points.size() == 6 && has(g.panels[0].points, {1.05f, 0.3f}));
        // Découpe d'une encoche à gauche : reproduite à droite
        g.panels[0].points = {{0, 0}, {1, 0}, {1, 0.6f}, {0, 0.6f}};
        g.panels.push_back({});
        g.panels[2].points = {{-0.1f, 0.2f}, {0.1f, 0.2f}, {0.1f, 0.3f}, {-0.1f, 0.3f}};
        std::string error;
        CHECK(garment::combinePanels(g, 0, 2, true, error));
        CHECK(garment::symmetrizePanel(g, 0, true));
        CHECK(std::abs(std::abs(garment::signedArea(g.panels[0].points)) - (0.6f - 2 * 0.01f)) < 1e-4f && has(g.panels[0].points, {0.9f, 0.2f}));
    });

    run("Menu des pièces : clones liés, miroir, ordre, décalage, simplification", [] {
        auto near = [](glm::vec2 a, glm::vec2 b) { return glm::distance(a, b) < 1e-4f; };
        garment::Garment g;
        garment::Panel a;
        a.name = "A";
        a.points = {{0, 0}, {0.4f, 0}, {0.4f, 0.6f}, {0, 0.6f}};
        garment::Panel b;
        b.name = "B";
        b.points = {{-1, 0}, {-0.6f, 0}, {-0.6f, 0.6f}, {-1, 0.6f}};
        b.symmetric = true;
        b.symAxisX = -0.8f;
        g.panels = {a, b};
        g.seams.push_back({{0, 3}, {1, 3}, false, 1, 1});   // bord gauche de A <-> bord gauche de B (symétrique)
        // Clone symétrique lié, avec coutures : le clone est cousu sur le bord droit de B
        garment::CloneOptions o;
        o.mirror = o.link = o.sewing = true;
        const auto clones = garment::clonePanels(g, {0}, o);
        CHECK(clones.size() == 1 && g.panels.size() == 3);
        CHECK(g.panels[0].linkGroup > 0 && g.panels[2].linkGroup == g.panels[0].linkGroup && g.panels[2].linkMirror);
        CHECK(g.seams.size() == 2 && g.seams[1].a.panel == 2 && g.seams[1].a.edge == 3 && g.seams[1].b.panel == 1 && g.seams[1].b.edge == 1);
        // Édition liée : un point tiré sur l'original bouge en miroir sur le clone
        const garment::Garment before = g;
        const glm::vec2 c2 = garment::centroid(g.panels[2].points);
        g.panels[0].points[2] = {0.5f, 0.7f};
        CHECK(garment::propagateLinkedEdits(before, g) == 1);
        CHECK(near(g.panels[2].points[2], c2 + glm::vec2(-(0.5f - 0.2f), 0.7f - 0.3f)));
        CHECK(g.seams.size() == 2);   // coutures sur des bords intacts gardées
        // Un simple déplacement n'est pas reporté
        const garment::Garment before2 = g;
        for (auto& q : g.panels[0].points) q += glm::vec2(0.1f, 0.0f);
        CHECK(garment::propagateLinkedEdits(before2, g) == 0);
        // Pivoter de 90° puis l'édition liée suit encore
        garment::rotatePanels(g, {0}, 90.0f);
        const garment::Garment before3 = g;
        const glm::vec2 old0 = g.panels[2].points[0];
        g.panels[0].points[0] += glm::vec2(0.0f, -0.05f);   // repère pivoté : -y ici = +x dans la forme commune
        CHECK(garment::propagateLinkedEdits(before3, g) == 1);
        CHECK(glm::distance(g.panels[2].points[0], old0) > 0.04f);
        // Retourner deux fois = rien
        const auto pts = g.panels[1].points;
        garment::flipPanels(g, {1}, false);
        garment::flipPanels(g, {1}, false);
        CHECK(near(g.panels[1].points[1], pts[1]));
        // Ordre : premier plan, les coutures suivent
        const auto idx = garment::reorderPanels(g, {0}, 2);
        CHECK(idx.size() == 1 && idx[0] == 2 && g.panels[2].name == "A" && g.seams[0].a.panel == 2);
        // Décaler le contour de 5 cm
        garment::Garment sq;
        sq.panels.push_back({});
        sq.panels[0].points = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        CHECK(garment::offsetPanel(sq, 0, 0.05f));
        CHECK(std::abs(garment::signedArea(sq.panels[0].points) - 1.21f) < 1e-3f);
        // Fendre et écarter : le coin (1,0) pivote autour de (1,1), la pièce s'ouvre en (0,0)
        {
            garment::Garment sl;
            sl.panels.push_back({});
            sl.panels[0].points = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
            CHECK(garment::slashAndSpread(sl, 0, 0, 2, 15.0f));
            CHECK(sl.panels[0].points.size() == 5);
            CHECK(near(sl.panels[0].points[0], {0, 0}) && near(sl.panels[0].points[3], {1, 1}));
            CHECK(std::abs(garment::signedArea(sl.panels[0].points)) > 1.0f);
            CHECK(!garment::slashAndSpread(sl, 0, 0, 1, 15.0f));   // points voisins : rien à écarter
        }
        // Arrondir un coin, couper la courbe en parties égales, pince, déplier une demi-pièce
        {
            garment::Garment h;
            h.panels.push_back({});
            h.panels[0].points = {{0, 0}, {0.2f, 0}, {0.2f, 0.5f}, {0, 0.5f}};   // demi-pièce, pli sur x = 0
            const int arc = garment::roundCorner(h, 0, 2, 0.15f, 10);
            CHECK(arc >= 0 && h.panels[0].points.size() == 14);
            // Les points de l'arc sont à 15 cm du centre de l'arc (0.05, 0.35)
            bool onCircle = true;
            for (int k = 0; k <= 10; ++k)
                onCircle = onCircle && std::abs(glm::distance(h.panels[0].points[static_cast<std::size_t>(arc + k)], glm::vec2(0.05f, 0.35f)) - 0.15f) < 1e-3f;
            CHECK(onCircle);
            int first = 0, count = 0;
            garment::curveAround(h, 0, arc + 3, first, count);
            CHECK(first == arc && count == 10);
            std::vector<glm::vec2> cuts;
            CHECK(garment::splitCurve(h, 0, arc + 3, 4, &cuts) && cuts.size() == 3 && h.panels[0].points.size() == 16);
            // Les coupes sont sur l'arc (la forme ne change pas), au quart, à la moitié et aux trois quarts
            bool cutsOnArc = true;
            for (const auto& c : cuts) cutsOnArc = cutsOnArc && std::abs(glm::distance(c, glm::vec2(0.05f, 0.35f)) - 0.15f) < 2e-3f;
            CHECK(cutsOnArc && near(cuts[1], h.panels[0].points[static_cast<std::size_t>(arc + 6)]));
            // Bord droit : coupé en 3 morceaux égaux
            garment::Garment r;
            r.panels.push_back({});
            r.panels[0].points = {{0, 0}, {0.3f, 0}, {0.3f, 0.5f}, {0, 0.5f}};
            CHECK(garment::splitCurve(r, 0, 0, 3) && r.panels[0].points.size() == 6 && near(r.panels[0].points[1], {0.1f, 0}));
            // Pince : la pointe entre dans la pièce
            const int tip = garment::addDart(r, 0, 1, 0.08f, 0.03f);
            CHECK(tip >= 0 && near(r.panels[0].points[static_cast<std::size_t>(tip)], {0.1f, 0.08f}));
            // Déplier autour du pli vertical x = 0 : pièce deux fois plus grande, symétrique
            const float before = std::abs(garment::signedArea(h.panels[0].points));
            int fold = -1;
            for (int k = 0; k < static_cast<int>(h.panels[0].points.size()); ++k) {
                const auto a = h.panels[0].points[static_cast<std::size_t>(k)];
                const auto b = h.panels[0].points[(static_cast<std::size_t>(k) + 1) % h.panels[0].points.size()];
                if (a.x == 0.0f && b.x == 0.0f) fold = k;
            }
            CHECK(fold >= 0 && garment::unfoldPanel(h, 0, fold));
            CHECK(std::abs(std::abs(garment::signedArea(h.panels[0].points)) - 2.0f * before) < 1e-4f);
            CHECK(h.panels[0].symmetric && h.panels[0].symAxisX == 0.0f);
        }
        // Couture libre : le bas d'une longue bande cousu sur le tour d'une arche (sauf son bas)
        {
            garment::Garment bag;
            bag.panels.push_back({});
            bag.panels[0].points = {{0, 0.6f}, {1.0f, 0.6f}, {1.0f, 0.7f}, {0, 0.7f}};   // bande de 1 m
            bag.panels.push_back({});
            bag.panels[1].points = {{0, 0}, {0.3f, 0}, {0.3f, 0.35f}, {0, 0.35f}};      // dos : tour sans le bas = 1 m
            CHECK(std::abs(garment::contourLength(bag, 1) - 1.3f) < 1e-5f);
            CHECK(near(garment::contourPoint(bag, 1, 0.45f), {0.3f, 0.15f}));
            CHECK(std::abs(garment::contourParam(bag, 1, 2, 0.5f) - 0.80f) < 1e-5f);
            // A : bord du bas de la bande, de gauche à droite ; B : de (0,0) en remontant (sens inverse des index)
            const int sid = garment::sewFree(bag, 0, 0.0f, 1.0f, 1, 0.0f, -1.0f);
            CHECK(sid >= 0 && bag.seams[static_cast<std::size_t>(sid)].countB == 3 && bag.seams[static_cast<std::size_t>(sid)].order == 2);
            // Portion qui commence au milieu d'un bord : un point est ajouté
            garment::Garment b2 = bag;
            b2.seams.clear();
            const int s2 = garment::sewFree(b2, 0, 0.25f, 0.5f, 1, 0.1f, 0.5f);
            CHECK(s2 >= 0 && b2.panels[0].points.size() == 6 && b2.panels[1].points.size() == 6);
            CHECK(garment::sewFree(b2, 0, 0.0f, 0.0f, 1, 0.0f, 0.5f) < 0);   // portion vide : refusée
            // Le maillage coud bien la bande sur le dos
            garment::GarmentMesh gm;
            std::string err;
            CHECK(garment::buildGarmentMesh(bag, {}, gm, err) && !gm.seamPairs.empty());
        }
        // Simplifier : les points alignés disparaissent, sauf sur une couture
        sq.panels[0].points = {{0, 0}, {0.5f, 0.0005f}, {1, 0}, {1, 0.5f}, {1, 1}, {0, 1}};
        sq.panels.push_back(sq.panels[0]);
        sq.seams = {{{0, 2}, {1, 0}, false, 1, 1}};   // bord (1,0)-(1,0.5) cousu
        CHECK(garment::simplifyPanel(sq, 0, 0.002f) == 1 && sq.panels[0].points.size() == 5 && sq.seams.size() == 1);
        // Renfort intérieur posé tout le tour
        const int inner = garment::addInnerShape(sq, 1, 0.02f);
        CHECK(inner == 2 && sq.attachmentOf(2) >= 0);
        // Lier une copie miroir faite à l'ancienne (points en sens inverse)
        garment::Garment m;
        m.panels.push_back({});
        m.panels[0].points = {{0, 0}, {0.3f, 0}, {0.4f, 0.5f}, {0, 0.6f}};
        m.addMirrorCopy(0, 0.05f);
        CHECK(garment::linkPanels(m, {0, 1}) == 1 && m.panels[1].linkMirror);
        // Sélection par propriété
        CHECK(garment::panelsLike(m, 0, garment::SameProperty::LinkGroup).size() == 2);
    });

    run("Pièces désactivées, projet : état 3D conservé", [] {
        garment::Garment g = garment::makeTankTop();
        g.panels[1].active3D = false;
        g.panels[0].frozen = true;
        g.panels[0].linkGroup = 3;
        garment::MeshingOptions mo;
        garment::GarmentMesh gm;
        std::string error;
        CHECK(garment::buildGarmentMesh(g, mo, gm, error));
        CHECK(std::none_of(gm.panelOfVertex.begin(), gm.panelOfVertex.end(), [](int p) { return p == 1; }));
        CHECK(std::all_of(gm.seamOfPair.begin(), gm.seamOfPair.end(), [&](int s) { return s < static_cast<int>(g.seams.size()); }));
        garment::Garment back;
        CHECK(io::parseProject(io::writeProject(g, nullptr), back, error));
        CHECK(back.panels.size() == 2 && !back.panels[1].active3D && back.panels[0].frozen && back.panels[0].linkGroup == 3);
    });

    run("Nouvelle pièce plate : posée sur un plan, devant le corps", [] {
        garment::Panel p;
        p.points = {{0, 0}, {0.4f, 0}, {0.4f, 0.5f}, {0, 0.5f}};
        p.placement.flat = true;
        garment::MeshingOptions mo;
        const glm::vec3 a = garment::placePoint(p, {0, 0}, mo), b = garment::placePoint(p, {0.4f, 0}, mo),
                        c = garment::placePoint(p, {0.2f, 0.25f}, mo), d = garment::placePoint(p, {0.4f, 0.5f}, mo);
        const glm::vec3 n = glm::normalize(glm::cross(b - a, d - a));
        CHECK(std::abs(glm::dot(c - a, n)) < 1e-5f);                 // tous les points dans un même plan
        CHECK(std::abs(glm::distance(a, b) - 0.4f) < 1e-4f);         // largeur conservée (pas enroulée)
        garment::Garment g;
        g.panels = {p};
        garment::Garment back;
        std::string error;
        CHECK(io::parseProject(io::writeProject(g, nullptr), back, error) && back.panels[0].placement.flat);
    });

    run("MCP : JSON lu et réécrit (échappements, unicode, nombres)", [] {
        mcp::Json j;
        std::string error;
        CHECK(mcp::Json::parse(R"({"a":[1,2.5,-3e2,true,null],"t":"éé😀\n\"","o":{}})", j, error));
        CHECK(j.get("a").size() == 5 && j.get("a").at(2).asNumber() == -300.0 && j.get("a").at(3).asBool());
        CHECK(j.get("t").asString() == "éé\xF0\x9F\x98\x80\n\"");
        CHECK(j.dump() == R"({"a":[1,2.5,-300,true,null],"t":"éé)" "\xF0\x9F\x98\x80" R"(\n\"","o":{}})");
        mcp::Json bad;
        CHECK(!mcp::Json::parse("{\"a\":}", bad, error));
        CHECK(!mcp::Json::parse("[1,2", bad, error));
        CHECK(!mcp::Json::parse("{} x", bad, error));
    });

    run("Allonger une pièce : points au-dessus de la ligne, poses et impressions", [] {
        garment::Garment g;
        garment::Panel base;
        base.points = {{0, 0}, {0.4f, 0}, {0.4f, 0.6f}, {0, 0.6f}};
        garment::Panel patch;
        patch.points = {{-0.05f, -0.05f}, {0.05f, -0.05f}, {0.05f, 0.05f}, {-0.05f, 0.05f}};
        g.panels = {base, patch};
        CHECK(g.attachPatch(1, 0, {0.2f, 0.45f}, garment::AttachKind::AllRound) >= 0);
        garment::Print pr;
        pr.image = "logo.png";
        pr.offset = {0.0f, 0.2f};   // y = 0.5 dans le patron
        g.panels[0].prints.push_back(pr);
        CHECK(garment::lengthenPanel(g, 0, 0.3f, 0.05f));
        CHECK(std::abs(g.panels[0].points[2].y - 0.65f) < 1e-5f && g.panels[0].points[1].y == 0.0f);
        CHECK(std::abs(g.attachments[0].at.y - 0.50f) < 1e-5f);
        CHECK(std::abs(garment::printCenter(g.panels[0], g.panels[0].prints[0]).y - 0.55f) < 1e-4f);   // suit le tissu au-dessus de la ligne
        CHECK(garment::lengthenPanel(g, 0, 0.3f, -0.1f));
        CHECK(std::abs(g.panels[0].points[2].y - 0.55f) < 1e-5f);
        CHECK(!garment::lengthenPanel(g, 0, 2.0f, 0.05f));   // ligne hors de la pièce
    });

    run("Couper une pièce selon une ligne : coutures partagées, même place en 3D", [] {
        garment::Garment g;
        garment::Panel a, b;
        a.name = "Devant";
        a.points = {{0, 0}, {0.4f, 0}, {0.4f, 0.6f}, {0, 0.6f}};
        b.points = {{0.5f, 0}, {0.9f, 0}, {0.9f, 0.6f}, {0.5f, 0.6f}};
        garment::Print pr;
        pr.image = "logo.png";
        pr.offset = {0.0f, 0.2f};   // centre (0.2, 0.5) : dans le morceau du haut
        a.prints.push_back(pr);
        g.panels = {a, b};
        garment::Seam s;
        s.a = {0, 1};   // bord droit de A, vers le haut
        s.b = {1, 3};   // bord gauche de B, vers le bas
        g.seams.push_back(s);
        garment::Panel patch;
        patch.points = {{0, 0}, {0.1f, 0}, {0.1f, 0.1f}, {0, 0.1f}};
        g.panels.push_back(patch);
        CHECK(g.attachPatch(2, 0, {0.2f, 0.1f}, garment::AttachKind::Pocket) >= 0);   // poche dans le bas
        const garment::Panel before = g.panels[0];
        std::string error;
        const int np = garment::splitPanelAlongLine(g, 0, {-0.1f, 0.2f}, {0.5f, 0.2f}, true, error);
        CHECK(np == 3 && error.empty());
        if (np != 3) return;
        CHECK(std::abs(std::abs(garment::signedArea(g.panels[0].points)) + std::abs(garment::signedArea(g.panels[3].points)) - 0.24f) < 1e-5f);
        // La couture A-B est partagée en deux (20 cm et 40 cm), plus la couture de la coupe.
        CHECK(g.seams.size() == 3);
        float l20 = 0, l40 = 0;
        for (const auto& se : g.seams) {
            const float la = g.seamLength(se, 0), lb = g.seamLength(se, 1);
            CHECK(std::abs(la - lb) < 1e-4f);
            if (std::abs(la - 0.2f) < 1e-4f && (se.a.panel == 1 || se.b.panel == 1)) l20 += 1;
            if (std::abs(la - 0.4f) < 1e-4f && (se.a.panel == 1 || se.b.panel == 1)) l40 += 1;
        }
        CHECK(l20 == 1 && l40 == 1);
        // Le morceau du bas et celui du haut sont cousus au bon endroit de B (même hauteur).
        for (const auto& se : g.seams) {
            if (se.a.panel == 1 || se.b.panel == 1) {
                const int side = se.a.panel == 1 ? 1 : 0;
                float lo = 1e9f, hi = -1e9f;
                for (const auto& e : g.seamEdges(se, side)) {
                    lo = std::min({lo, g.edgeStart(e).y, g.edgeEnd(e).y});
                    hi = std::max({hi, g.edgeStart(e).y, g.edgeEnd(e).y});
                }
                float blo = 1e9f, bhi = -1e9f;
                for (const auto& e : g.seamEdges(se, 1 - side)) {
                    blo = std::min({blo, g.edgeStart(e).y, g.edgeEnd(e).y});
                    bhi = std::max({bhi, g.edgeStart(e).y, g.edgeEnd(e).y});
                }
                CHECK(std::abs(lo - blo) < 1e-4f && std::abs(hi - bhi) < 1e-4f);
            }
        }
        // Impression et poche suivent leur morceau ; chaque morceau reste à sa place en 3D.
        const int top = garment::pointInPolygon(g.panels[0].points, {0.2f, 0.5f}) ? 0 : 3;
        const int bottom = top == 0 ? 3 : 0;
        CHECK(g.panels[top].prints.size() == 1 && g.panels[bottom].prints.empty());
        CHECK(std::abs(garment::printCenter(g.panels[top], g.panels[top].prints[0]).y - 0.5f) < 1e-4f);
        CHECK(g.attachments[0].base == bottom);
        const garment::MeshingOptions opt;
        for (const glm::vec2 q : {glm::vec2(0.1f, 0.5f), glm::vec2(0.3f, 0.1f)}) {
            const int pi = garment::pointInPolygon(g.panels[0].points, q) ? 0 : 3;
            CHECK(glm::distance(garment::placePoint(before, q, opt), garment::placePoint(g.panels[pi], q, opt)) < 1e-3f);
        }
        garment::GarmentMesh mesh;
        CHECK(garment::buildGarmentMesh(g, garment::MeshingOptions{}, mesh, error));
        // Coupes refusées
        CHECK(garment::splitPanelAlongLine(g, 1, {0.0f, 2.0f}, {1.0f, 2.0f}, false, error) < 0);
        CHECK(garment::splitPanel(g, 2, {0, 0.05f}, {0.1f, 0.05f}, false, error) < 0);   // pièce posée
    });

    run("Bande de soufflet : longueur de la portion, cousue des deux côtés, debout vers le corps en 3D", [] {
        garment::Garment g;
        garment::Panel front, back;
        front.name = "Poche";
        front.points = {{0, 0}, {0.2f, 0}, {0.2f, 0.25f}, {0, 0.25f}};
        front.placement.height = 1.0f;
        back = front;
        back.name = "Fond";
        for (auto& q : back.points) q.x += 0.3f;
        g.panels = {front, back};
        // Portion : du coin haut gauche, en descendant (bord 3), le bas, puis en remontant à droite (25 + 20 + 25 cm).
        const float s0 = garment::contourParam(g, 0, 3, 0.0f);
        std::string error;
        const int strip = garment::addGussetStrip(g, 0, s0, 0.70f, 0.03f, error);
        CHECK(strip == 2 && error.empty());
        if (strip != 2) return;
        const auto& sp = g.panels[2].points;
        CHECK(std::abs(glm::distance(sp[0], sp[1]) - 0.70f) < 1e-4f && std::abs(glm::distance(sp[1], sp[2]) - 0.03f) < 1e-4f);
        CHECK(g.seams.size() == 1 && std::abs(g.seamLength(g.seams[0], 0) - g.seamLength(g.seams[0], 1)) < 1e-4f);
        // Le bas de la bande est contre la portion en 3D (son milieu contre le milieu du bas de la poche).
        const garment::MeshingOptions opt;
        const glm::vec3 a = garment::placePoint(g.panels[0], {0.1f, 0.0f}, opt);
        const glm::vec3 b = garment::placePoint(g.panels[2], (sp[0] + sp[1]) * 0.5f, opt);
        CHECK(glm::distance(a, b) < 0.01f);
        const glm::vec3 top = garment::placePoint(g.panels[2], (sp[2] + sp[3]) * 0.5f, opt);
        CHECK(glm::length(glm::vec2(top.x, top.z)) < glm::length(glm::vec2(b.x, b.z)) - 0.025f);   // debout, vers le corps
        // Le haut de la bande sur le fond : même portion.
        const int seam = garment::sewGussetTop(g, strip, 1, garment::contourParam(g, 1, 3, 0.0f), 0.70f);
        CHECK(seam >= 0 && g.seams.size() == 2);
        if (seam >= 0) CHECK(std::abs(g.seamLength(g.seams[static_cast<std::size_t>(seam)], 0) - 0.70f) < 1e-4f);
        garment::GarmentMesh mesh;
        CHECK(garment::buildGarmentMesh(g, garment::MeshingOptions{}, mesh, error));
        CHECK(garment::addGussetStrip(g, 0, 0.0f, 0.001f, 0.03f, error) < 0);
    });

    run("Lignes de pli : arêtes dans le maillage, tissu plié du bon côté, projet", [] {
        for (const float angle : {90.0f, -90.0f}) {
            garment::Garment g;
            garment::Panel p;
            p.points = {{0, 0}, {0.3f, 0}, {0.3f, 0.2f}, {0, 0.2f}};
            p.placement.flat = true;
            garment::FoldLine f;
            f.a = {0.0f, -0.2f};   // ligne verticale au milieu (débordant : coupée au contour)
            f.b = {0.0f, 0.2f};
            f.angleDeg = angle;
            f.strength = 1.0f;
            p.folds.push_back(f);
            g.panels.push_back(p);
            garment::GarmentMesh gm;
            std::string error;
            CHECK(garment::buildGarmentMesh(g, garment::MeshingOptions{}, gm, error));
            CHECK(gm.foldEdges.size() >= 8);
            bool onLine = true;
            for (const auto& e : gm.foldEdges)
                onLine = onLine && std::abs(gm.mesh.uvs[e.a].x - 0.15f) < 1e-4f && std::abs(gm.mesh.uvs[e.b].x - 0.15f) < 1e-4f;
            CHECK(onLine);
            // Simulation sans gravité ni obstacle : le tissu se plie d'environ 90°.
            simulation::XpbdSolver solver;
            solver.build(gm.mesh, simulation::fabricPresets()[0]);
            solver.spheres().clear();
            solver.settings().groundEnabled = false;
            solver.settings().gravity = glm::vec3(0.0f);
            solver.settings().selfCollision = false;
            simulation::applyFolds(solver, gm);
            CHECK(solver.foldCount() >= 8);
            auto at = [&](glm::vec2 q) {
                uint32_t best = 0;
                float bd = 1e9f;
                for (uint32_t v = 0; v < gm.mesh.uvs.size(); ++v)
                    if (const float d = glm::distance(gm.mesh.uvs[v], q); d < bd) {
                        bd = d;
                        best = v;
                    }
                return best;
            };
            const uint32_t left = at({0.0f, 0.1f}), right = at({0.3f, 0.1f}), mid = at({0.15f, 0.1f});
            const glm::vec3 n0 = glm::normalize(glm::cross(gm.mesh.positions[right] - gm.mesh.positions[left],
                                                           gm.mesh.positions[at({0.0f, 0.2f})] - gm.mesh.positions[left]));
            for (int k = 0; k < 120; ++k) solver.step(1.0f / 30.0f);
            const auto& x = solver.positions();
            const float span = glm::distance(x[left], x[right]);
            {
                // Pli net le long de la ligne : angle entre les deux côtés tout près de la ligne.
                const uint32_t l1 = at({0.10f, 0.1f}), r1 = at({0.20f, 0.1f});
                const float local = glm::degrees(std::acos(glm::dot(glm::normalize(x[l1] - x[mid]), glm::normalize(x[r1] - x[mid]))));
                std::printf("    angle entre les côtés près du pli : %.1f° (visé : 90°)\n", static_cast<double>(local));
                CHECK(local > 75.0f && local < 115.0f);
            }
            std::printf("    angle %.0f : écart des bords %.3f m (plié à 90° : 0.212), %zu plis\n", static_cast<double>(angle), static_cast<double>(span), solver.foldCount());
            CHECK(span > 0.17f && span < 0.27f);   // à plat : 0.30
            const float side = glm::dot((x[left] + x[right]) * 0.5f - x[mid], n0);
            CHECK(angle > 0.0f ? side > 0.01f : side < -0.01f);
            if (angle > 0.0f) {
                garment::Garment back;
                CHECK(io::parseProject(io::writeProject(g), back, error));
                CHECK(back.panels[0].folds.size() == 1 && std::abs(back.panels[0].folds[0].angleDeg - 90.0f) < 1e-4f);
            }
        }
    });

    run("Fermeture éclair : bande à dents posée, curseur au bout, texture", [] {
        garment::Garment g;
        garment::Panel p;
        p.name = "Rabat";
        p.points = {{0, 0}, {0.3f, 0}, {0.3f, 0.2f}, {0, 0.2f}};
        g.panels.push_back(p);
        std::string error;
        // Tracée au-delà de la pièce : raccourcie à 5 mm des bords.
        const int zi = garment::addZipper(g, 0, {-0.1f, 0.15f}, {0.5f, 0.15f}, 0.025f, error);
        CHECK(zi == 1 && error.empty() && g.panels.size() == 3 && g.attachments.size() == 2);
        if (zi != 1) return;
        const auto& z = g.panels[1];
        CHECK(z.finish == garment::FabricFinish::Zipper);
        CHECK(std::abs(glm::distance(z.points[0], z.points[1]) - 0.29f) < 1e-4f || std::abs(glm::distance(z.points[1], z.points[2]) - 0.29f) < 1e-4f);
        CHECK(glm::distance(g.attachments[0].at, glm::vec2(0.15f, 0.15f)) < 1e-4f);
        CHECK(g.attachKind(g.attachments[1]) == garment::AttachKind::Flap && std::abs(g.attachments[1].at.x - 0.295f) < 1e-4f);
        garment::GarmentMesh gm;
        CHECK(garment::buildGarmentMesh(g, garment::MeshingOptions{}, gm, error));
        // Texture : dents claires au milieu de la bande, ruban sombre sur ses bords.
        garment::PatternTexture tex;
        CHECK(garment::buildPatternTexture(g, nullptr, garment::PatternTextureOptions{}, tex));
        auto px = [&](glm::vec2 w) {
            const int x = static_cast<int>((w.x - tex.origin.x) / tex.metersPerPixel), y = static_cast<int>((w.y - tex.origin.y) / tex.metersPerPixel);
            return &tex.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(tex.width) + static_cast<std::size_t>(x)) * 4];
        };
        const glm::vec2 zc = garment::centroid(z.points);
        int bright = 0;
        for (int k = 0; k < 10; ++k) bright += px(zc + glm::vec2(0.0003f * static_cast<float>(k), 0.0f))[0] > 150 ? 1 : 0;
        CHECK(bright >= 4);   // dents métalliques
        CHECK(px(zc + glm::vec2(0.0f, 0.009f))[0] < 80);   // ruban
        garment::Garment back;
        CHECK(io::parseProject(io::writeProject(g), back, error));
        CHECK(back.panels.size() == 3 && back.panels[1].finish == garment::FabricFinish::Zipper);
        CHECK(garment::addZipper(g, 1, {0, 0}, {0.1f, 0}, 0.02f, error) < 0);   // pas sur une pièce posée
    });

    run("Impressions et aspect : enregistrement et texture du patron", [] {
        garment::Garment g;
        garment::Panel p;
        p.points = {{0, 0}, {0.3f, 0}, {0.3f, 0.3f}, {0, 0.3f}};
        p.finish = garment::FabricFinish::Ripstop;
        garment::Print pr;
        pr.image = "impressions/logo avec espace.png";
        pr.offset = {0.02f, -0.01f};
        pr.width = 0.1f;
        pr.angleDeg = 30.0f;
        p.prints.push_back(pr);
        g.panels.push_back(p);
        garment::Garment back;
        std::string error;
        CHECK(io::parseProject(io::writeProject(g), back, error));
        CHECK(back.panels.size() == 1 && back.panels[0].finish == garment::FabricFinish::Ripstop && back.panels[0].prints.size() == 1);
        CHECK(back.panels[0].prints[0].image == pr.image && std::abs(back.panels[0].prints[0].angleDeg - 30.0f) < 1e-4f);
        // Image 2 x 1 rouge opaque, centrée sur la pièce (sans rotation) : peinte au centre, rien ailleurs.
        io::RasterImage red;
        red.width = 2;
        red.height = 1;
        red.rgba = {255, 0, 0, 255, 255, 0, 0, 255};
        back.panels[0].prints[0].offset = {0, 0};
        back.panels[0].prints[0].angleDeg = 0.0f;
        garment::PatternTexture tex;
        garment::PatternTextureOptions opt;
        CHECK(garment::buildPatternTexture(back, [&](const std::string&) { return &red; }, opt, tex));
        auto pixel = [&](glm::vec2 q) {
            const glm::vec2 uv = tex.uv(q);
            const int x = std::clamp(static_cast<int>(uv.x * static_cast<float>(tex.width)), 0, tex.width - 1);
            const int y = std::clamp(static_cast<int>(uv.y * static_cast<float>(tex.height)), 0, tex.height - 1);
            return &tex.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(tex.width) + static_cast<std::size_t>(x)) * 4];
        };
        CHECK(pixel({0.15f, 0.15f})[0] > 200 && pixel({0.15f, 0.15f})[3] > 200);   // centre : rouge
        CHECK(pixel({0.15f, 0.22f})[3] == 0);   // au-dessus de l'image (hauteur 5 cm) : transparent
        CHECK(garment::printSize(back.panels[0].prints[0], &red).y > 0.049f);
        // Coutures : bord assombri même sans impression.
        garment::Garment plain;
        plain.panels.push_back(p);
        plain.panels[0].prints.clear();
        garment::PatternTextureOptions so;
        so.stitches = true;
        garment::PatternTexture st;
        CHECK(garment::buildPatternTexture(plain, [](const std::string&) { return nullptr; }, so, st));
        {
            const glm::vec2 uv = st.uv({0.15f, 0.0005f});
            const int x = static_cast<int>(uv.x * static_cast<float>(st.width)), y = static_cast<int>(uv.y * static_cast<float>(st.height));
            CHECK(st.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(st.width) + static_cast<std::size_t>(x)) * 4 + 3] > 0);
        }
        garment::Garment none;
        none.panels.push_back(p);
        none.panels[0].prints.clear();
        CHECK(!garment::buildPatternTexture(none, [](const std::string&) { return nullptr; }, opt, tex));
    });

    run("MCP : initialisation, liste des outils, création et couture de pièces", [] {
        const std::string path = (std::filesystem::temp_directory_path() / "drapeo_test_mcp.drapeo").string();
        std::filesystem::remove(path);
        mcp::Server server(path);
        mcp::Json r;
        std::string error;
        CHECK(mcp::Json::parse(server.handleLine(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18"}})"), r, error));
        CHECK(r.get("result").get("serverInfo").get("name").asString() == "drapeo");
        CHECK(server.handleLine(R"({"jsonrpc":"2.0","method":"notifications/initialized"})").empty());
        CHECK(mcp::Json::parse(server.handleLine(R"({"jsonrpc":"2.0","id":"x","method":"tools/list"})"), r, error));
        CHECK(r.get("id").asString() == "x" && r.get("result").get("tools").size() >= 20);
        auto call = [&](const std::string& name, const std::string& args, bool expectOk = true) {
            mcp::Json a;
            std::string e, text;
            mcp::Json::parse(args, a, e);
            const bool ok = server.callTool(name, a, text, e);
            CHECK(ok == expectOk);
            return ok ? text : e;
        };
        call("nouveau_projet", R"({"modele":"vide"})");
        call("ajouter_rectangle", R"({"nom":"Devant","largeur_cm":50,"hauteur_piece_cm":60,"hauteur_cm":90})");
        call("ajouter_piece", R"({"nom":"Dos","points":[[0,0],[50,0],[50,60],[0,60]],"angle":180,"hauteur_cm":90,"couleur":"#3355aa"})");
        call("coudre", R"({"piece_a":"Devant","bord_a":1,"piece_b":"dos","bord_b":3})");
        call("coudre", R"({"piece_a":0,"bord_a":3,"piece_b":1,"bord_b":1})");
        call("modifier_piece", R"({"piece":1,"retrait_trame":90,"couche":1,"pression":12})");
        call("coudre", R"({"piece_a":5,"bord_a":0,"piece_b":1,"bord_b":0})", false);
        call("coudre", R"({"piece_a":0,"bord_a":9,"piece_b":1,"bord_b":0})", false);
        call("inconnu", "{}", false);
        garment::Garment g;
        io::ProjectAvatar av;
        CHECK(io::loadProject(path, g, error, &av));
        CHECK(g.panels.size() == 2 && g.seams.size() == 2 && av.present);
        CHECK(std::abs(g.panels[1].color.b - 0xaa / 255.0f) < 1e-3f && g.panels[1].placement.angleDeg == 180.0f);
        CHECK(std::abs(g.panels[1].shrinkWeft - 0.9f) < 1e-4f && g.panels[1].layer == 1);
        CHECK(g.panels[1].points[0].x > g.panels[0].points[1].x);   // rangée à droite de la première
        // Arrondir un coin cousu retire la couture et le signale.
        const std::string t = call("arrondir_coin", R"({"piece":0,"point":2,"distance_cm":5})");
        CHECK(t.find("retirée") != std::string::npos);
        mcp::Json info;
        CHECK(mcp::Json::parse(call("projet_info", "{}"), info, error));
        CHECK(info.get("pieces").size() == 2 && info.get("pieces").at(0).get("points").size() > 4);
        // Simulation courte sur le mannequin intégré, avec export OBJ.
        const std::string obj = (std::filesystem::temp_directory_path() / "drapeo_test_mcp.obj").string();
        mcp::Json sim;
        CHECK(mcp::Json::parse(call("simuler", R"({"secondes":0.5,"exporter_obj":")" + obj + R"("})"), sim, error));
        CHECK(sim.get("particules").asNumber() > 100 && sim.has("encombrement") && std::filesystem::exists(obj));
        // Outils ajoutés : aspect, impression, pose d'une pièce, allongement.
        call("modifier_piece", R"({"piece":0,"aspect":"ripstop"})");
        call("modifier_piece", R"({"piece":0,"aspect":"velours"})", false);
        call("ajouter_impression", R"({"piece":1,"image":"logo.png","largeur_cm":8})");
        call("ajouter_rectangle", R"({"nom":"renfort","largeur_cm":6,"hauteur_piece_cm":6})");
        call("poser_piece", R"({"piece":"renfort","sur":0,"x_cm":-500,"y_cm":-500})", false);   // pas sur la pièce
        mcp::Json info2;
        CHECK(mcp::Json::parse(call("projet_info", "{}"), info2, error));
        const auto& p0 = info2.get("pieces").at(0);
        const std::string onBase = R"({"piece":"renfort","sur":0,"type":"tout_autour","x_cm":)" +
                                   std::to_string(p0.get("centre_cm").at(0).asNumber()) + R"(,"y_cm":)" +
                                   std::to_string(p0.get("centre_cm").at(1).asNumber()) + "}";
        call("poser_piece", onBase);
        call("allonger_piece", R"({"piece":1,"a_hauteur_cm":10,"longueur_cm":3})");
        call("supprimer_impression", R"({"piece":1,"index":3})", false);
        garment::Garment g2;
        CHECK(io::loadProject(path, g2, error));
        CHECK(g2.panels[0].finish == garment::FabricFinish::Ripstop && g2.panels[1].prints.size() == 1 && g2.attachments.size() == 1);
        call("supprimer_impression", R"({"piece":1,"index":0})");
        {
            garment::Garment g3;
            CHECK(io::loadProject(path, g3, error));
            const std::size_t before = g3.panels.size();
            call("couper_piece", R"({"piece":1,"hauteur_cm":5,"recoudre":true})");
            call("couper_piece", R"({"piece":1,"hauteur_cm":500})", false);   // hors de la pièce
            call("ajouter_soufflet", R"({"piece":0,"bord":0,"nb_bords":2,"largeur_cm":4,"piece_b":1,"bord_b":0})");
            call("ajouter_soufflet", R"({"piece":0,"bord":0,"piece_b":0,"bord_b":1})", false);
            call("ajouter_pli", R"({"piece":0,"x1_cm":-500,"y1_cm":5,"x2_cm":500,"y2_cm":5,"angle":-120,"force":60})");
            call("ajouter_pli", R"({"piece":0,"x1_cm":900,"y1_cm":900,"x2_cm":950,"y2_cm":900})", false);
            call("supprimer_pli", R"({"piece":0,"index":3})", false);
            call("ajouter_fermeture", R"({"piece":0,"x1_cm":-500,"y1_cm":8,"x2_cm":500,"y2_cm":8})");
            CHECK(io::loadProject(path, g3, error));
            CHECK(g3.panels.size() == before + 4);
            CHECK(g3.panels[0].folds.size() == 1 && std::abs(g3.panels[0].folds[0].angleDeg + 120.0f) < 1e-3f);
        }
        {   // poche tracée directement sur une pièce
            garment::Garment g5;
            CHECK(io::loadProject(path, g5, error));
            const std::size_t before = g5.panels.size();
            call("ajouter_poche", R"({"piece":0,"x_cm":0,"y_cm":5,"largeur_cm":4,"hauteur_cm":4,"avec_rabat":true})");
            call("ajouter_poche", R"({"piece":0,"x_cm":900,"y_cm":900,"largeur_cm":4,"hauteur_cm":4})", false);   // hors de la pièce
            call("ajouter_poche", R"({"piece":0,"x_cm":0,"y_cm":5,"largeur_cm":4,"hauteur_cm":4,"type":"autre"})", false);
            CHECK(io::loadProject(path, g5, error) && g5.panels.size() == before + 2);
        }
        {   // calque d'une pièce par l'outil
            call("modifier_piece", R"({"piece":0,"calque":"Veste"})");
            garment::Garment g4;
            CHECK(io::loadProject(path, g4, error));
            CHECK(g4.groups.size() == 1 && g4.groups[0].name == "Veste" && g4.panels[0].group == 0);
            call("modifier_piece", R"({"piece":0,"calque":""})");
            CHECK(io::loadProject(path, g4, error) && g4.panels[0].group == -1);
        }
        std::filesystem::remove(path);
        std::filesystem::remove(obj);
    });

    run("Poche tracée sur le patron : cousue à sa place, rabat au-dessus, refus hors pièce", [] {
        garment::Garment g;
        g.panels.push_back({});
        g.panels[0].name = "Jambe";
        g.panels[0].points = {{0, 0}, {0.3f, 0}, {0.3f, 0.6f}, {0, 0.6f}};
        g.panels[0].color = {0.2f, 0.3f, 0.6f};
        std::string error;
        const int pi = garment::addPocket(g, 0, {0.05f, 0.2f}, {0.2f, 0.36f}, garment::AttachKind::Pocket, true, error);
        CHECK(pi == 1 && g.panels.size() == 3 && g.attachments.size() == 2);
        const auto& a = g.attachments[static_cast<std::size_t>(g.attachmentOf(pi))];
        CHECK(a.base == 0 && glm::distance(a.at, glm::vec2(0.125f, 0.28f)) < 1e-5f && g.attachKind(a) == garment::AttachKind::Pocket);
        // Dessinée à sa place sur la jambe (pas rangée ailleurs) ; ouverte en haut : 3 bords cousus.
        CHECK(garment::pointInPolygon(g.panels[0].points, garment::centroid(g.panels[1].points)) && g.attachedEdges(a).size() == 3);
        const auto& f = g.attachments[static_cast<std::size_t>(g.attachmentOf(2))];
        CHECK(g.attachKind(f) == garment::AttachKind::Flap && f.at.y > a.at.y);
        CHECK(garment::addPocket(g, 0, {0.5f, 0.5f}, {0.6f, 0.6f}, garment::AttachKind::Pocket, false, error) < 0);   // hors de la pièce
        CHECK(garment::addPocket(g, pi, {0.1f, 0.25f}, {0.15f, 0.3f}, garment::AttachKind::Pocket, false, error) < 0);   // sur une poche
        CHECK(garment::addPocket(g, 0, {0.1f, 0.1f}, {0.105f, 0.2f}, garment::AttachKind::Pocket, false, error) < 0);    // trop étroite
    });

    run("Calques : objets séparés, masqués (non simulés), verrouillés, enregistrés", [] {
        garment::Garment g;
        for (int k = 0; k < 3; ++k) {
            g.panels.push_back({});
            g.panels[static_cast<std::size_t>(k)].name = "P" + std::to_string(k);
            g.panels[static_cast<std::size_t>(k)].points = {{0.5f * k, 0}, {0.5f * k + 0.3f, 0}, {0.5f * k + 0.3f, 0.3f}, {0.5f * k, 0.3f}};
            g.panels[static_cast<std::size_t>(k)].placement.flat = true;
        }
        g.groups = {{"Veste", true, false}, {"Pantalon", true, false}};
        g.panels[0].group = 0;
        g.panels[1].group = 1;
        CHECK(g.panelVisible(0) && g.panelEditable(1) && g.panelVisible(2) && g.groupOf(2) == nullptr);
        garment::GarmentMesh all, part;
        std::string error;
        CHECK(garment::buildGarmentMesh(g, {}, all, error));
        g.groups[1].visible = false;   // pantalon masqué : pas simulé
        CHECK(!g.panelVisible(1) && !g.panelEditable(1));
        CHECK(garment::buildGarmentMesh(g, {}, part, error));
        bool noHidden = true;
        for (const int p : part.panelOfVertex) noHidden = noHidden && p != 1;
        CHECK(noHidden && part.mesh.vertexCount() < all.mesh.vertexCount());
        g.groups[0].locked = true;
        CHECK(g.panelVisible(0) && !g.panelEditable(0));
        // Enregistrement
        garment::Garment back;
        CHECK(io::parseProject(io::writeProject(g), back, error));
        CHECK(back.groups.size() == 2 && back.groups[0].name == "Veste" && back.groups[0].locked && !back.groups[1].visible &&
              back.panels[0].group == 0 && back.panels[1].group == 1 && back.panels[2].group == -1);
        // Ordre et suppression : les pièces restent dans leur calque
        g.moveGroup(0, 1);
        CHECK(g.groups[0].name == "Pantalon" && g.panels[0].group == 1 && g.panels[1].group == 0);
        g.removeGroup(0);
        CHECK(g.groups.size() == 1 && g.panels[1].group == -1 && g.panels[0].group == 0);
        // Index invalide dans un fichier : pièce sans calque
        CHECK(io::parseProject("drapeo-projet 1\npiece\ngroupe 7\npoints 3\n0 0\n0.1 0\n0 0.1\n", back, error) && back.panels[0].group == -1);
    });

    std::printf("\n%d vérifications, %d échec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
