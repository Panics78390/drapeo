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

#include "garment/BodyAxes.h"
#include "garment/Pattern.h"
#include "geometry/ClothMesh.h"

namespace drapeo::garment {

struct GarmentMesh {
    geometry::ClothMesh mesh;                       // positions 3D placées, uvs = coordonnées 2D du patron
    std::vector<int> panelOfVertex;
    std::vector<std::array<uint32_t, 2>> seamPairs; // sommets à réunir (coutures actives)
    std::vector<std::array<uint32_t, 2>> allSeamPairs; // toutes les coutures, actives ou non
    std::vector<int> seamOfPair;                    // couture de chaque paire de allSeamPairs
    std::vector<std::vector<glm::vec2>> panelPoints2D; // pour l'export
    /// Arêtes du maillage sur les lignes de pli (le tissu se plie autour d'elles).
    struct FoldEdge {
        uint32_t a, b;
        float angleDeg, strength;
    };
    std::vector<FoldEdge> foldEdges;
};

struct MeshingOptions {
    float spacing = 0.02f;     // distance cible entre particules (m)
    float bodyScale = 1.0f;    // hauteur avatar / 1,75 m : adapte le placement
    glm::vec3 bodyCenter{0.0f};
    BodyAxes axes;             // axes du buste et des membres (avatar de 1,75 m)
};

/// Construit le maillage. Renvoie false (et `error`) si une pièce est invalide.
bool buildGarmentMesh(const Garment& garment, const MeshingOptions& options, GarmentMesh& out, std::string& error);

/// Repère d'une zone du corps : axe (origine, direction vers le haut) et
/// directions devant / côté (angle 0 = devant, 90 = côté). Les pièces de cette
/// zone sont enroulées sur un cylindre autour de cet axe.
struct ZoneFrame {
    glm::vec3 origin{0.0f}, up{0, 1, 0}, front{0, 0, 1}, side{1, 0, 0};
};
ZoneFrame zoneFrame(BodyZone zone, const MeshingOptions& options);

/// Position 3D d'un point 2D d'une pièce selon son placement.
glm::vec3 placePoint(const Panel& panel, glm::vec2 p, const MeshingOptions& options);

/// Position 3D d'un point d'une pièce du vêtement (une pièce posée suit sa base).
glm::vec3 placeGarmentPoint(const Garment& garment, int panel, glm::vec2 p, const MeshingOptions& options);

/// Centre de rotation d'une pièce (centre de son contour enroulé, sans le décalage libre).
glm::vec3 panelPivot(const Panel& panel, const MeshingOptions& options);

/// Paires des coutures actives seulement.
std::vector<std::array<uint32_t, 2>> enabledSeamPairs(const Garment& garment, const GarmentMesh& mesh);

} // namespace drapeo::garment
