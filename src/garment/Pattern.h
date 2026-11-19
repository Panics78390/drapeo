// =============================================================================
//  garment/Pattern — modèle de données d'un vêtement
//
//  Un vêtement = des pièces de patron 2D (polygones, en mètres, vues de
//  l'extérieur du vêtement) + des coutures qui relient deux bords + un
//  placement par pièce autour du corps (« point d'arrangement » : la pièce est
//  enroulée sur un cylindre vertical centré sur l'avatar).
// =============================================================================
#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace drapeo::garment {

struct Placement {
    float angleDeg = 0.0f;   // 0 = devant, 180 = dos, 90 = côté gauche de l'avatar (+X)
    float height = 1.0f;     // hauteur du bas de la pièce (m), pour un avatar de 1,75 m
    float radius = 0.24f;    // rayon du cylindre d'enroulement (m)
};

struct Panel {
    std::string name;
    std::vector<glm::vec2> points;   // contour fermé, sens quelconque
    glm::vec3 color{0.85f, 0.83f, 0.78f};
    Placement placement;
};

/// Un bord = le segment entre points[edge] et points[(edge + 1) % n] de la pièce.
struct EdgeRef {
    int panel = -1;
    int edge = -1;
    bool operator==(const EdgeRef& o) const { return panel == o.panel && edge == o.edge; }
};

struct Seam {
    EdgeRef a, b;
    bool flipped = false;   // inverse le sens d'appariement choisi automatiquement
};

struct Garment {
    std::vector<Panel> panels;
    std::vector<Seam> seams;

    [[nodiscard]] bool empty() const { return panels.empty(); }
    [[nodiscard]] glm::vec2 edgeStart(const EdgeRef& e) const;
    [[nodiscard]] glm::vec2 edgeEnd(const EdgeRef& e) const;
    [[nodiscard]] float edgeLength(const EdgeRef& e) const;

    // Édition qui garde les coutures cohérentes.
    void removePanel(int panel);
    void removePoint(int panel, int point);
    void insertPoint(int panel, int edge, glm::vec2 position);   // coupe le bord `edge` en deux
    void removeSeamsOn(const EdgeRef& e);
    int addMirrorCopy(int panel, float gap);                       // copie miroir à droite, renvoie son index
};

// --- Géométrie 2D utilitaire -------------------------------------------------
float signedArea(const std::vector<glm::vec2>& poly);
bool pointInPolygon(const std::vector<glm::vec2>& poly, glm::vec2 p);
float distanceToSegment(glm::vec2 p, glm::vec2 a, glm::vec2 b, float* t = nullptr);
glm::vec2 centroid(const std::vector<glm::vec2>& poly);

// --- Modèles prêts à l'emploi (avatar de 1,75 m) ------------------------------
Garment makeTankTop();
Garment makeSkirt();
Garment makeDress();

} // namespace drapeo::garment
