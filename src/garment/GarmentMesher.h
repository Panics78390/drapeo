// =============================================================================
//  garment/GarmentMesher — des patrons 2D au maillage 3D simulable
//
//  1. Chaque bord est échantillonné à l'espacement voulu ; les deux bords
//     d'une couture reçoivent le MÊME nombre de points, pour que la couture se
//     fasse sommet à sommet.
//  2. Des points intérieurs sur une grille régulière complètent la pièce.
//  3. Triangulation de Delaunay contrainte (bibliothèque CDT) : les bords du
//     patron sont respectés exactement, les triangles sont bien formés.
//  4. Placement : la pièce est enroulée sur un cylindre autour du corps.
//  5. Coutures : paires de sommets, sens choisi pour minimiser la distance 3D.
// =============================================================================
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "garment/Pattern.h"
#include "geometry/ClothMesh.h"

namespace drapeo::garment {

struct GarmentMesh {
    geometry::ClothMesh mesh;                       // positions 3D placées, uvs = coordonnées 2D du patron
    std::vector<int> panelOfVertex;
    std::vector<std::array<uint32_t, 2>> seamPairs; // sommets à réunir
    std::vector<std::vector<glm::vec2>> panelPoints2D; // pour l'export
};

struct MeshingOptions {
    float spacing = 0.02f;     // distance cible entre particules (m)
    float bodyScale = 1.0f;    // hauteur avatar / 1,75 m : adapte le placement
    glm::vec3 bodyCenter{0.0f};
};

/// Construit le maillage. Renvoie false (et `error`) si une pièce est invalide.
bool buildGarmentMesh(const Garment& garment, const MeshingOptions& options, GarmentMesh& out, std::string& error);

/// Position 3D d'un point 2D d'une pièce selon son placement.
glm::vec3 placePoint(const Panel& panel, glm::vec2 p, const MeshingOptions& options);

} // namespace drapeo::garment
