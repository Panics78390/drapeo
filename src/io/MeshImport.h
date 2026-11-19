// =============================================================================
//  io/MeshImport — import d'avatar FBX et OBJ (via la bibliothèque ufbx)
//
//  Le résultat est un maillage triangulé unique, en mètres, axe Y vers le haut,
//  tous les objets du fichier fusionnés dans l'espace monde (pose de liaison
//  pour les personnages riggés).
// =============================================================================
#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace drapeo::io {

struct ImportedMesh {
    std::vector<glm::vec3> positions;
    std::vector<uint32_t>  indices;
    std::string name;            // nom du fichier
    std::size_t sourceObjects = 0;
    float sourceHeight = 0.0f;   // hauteur après conversion d'unités (m)
};

/// Charge un fichier .fbx ou .obj. En cas d'échec, renvoie false et remplit `error`.
bool importMesh(const std::string& utf8Path, ImportedMesh& out, std::string& error);

struct FitOptions {
    float targetHeight = 0.0f;   // 0 = garder l'échelle du fichier si plausible (0,3 à 3 m), sinon 1,75 m
    float rotationYDegrees = 0.0f;
};

/// Pose le maillage au sol (y = 0), centré en X/Z, à la bonne taille.
/// Renvoie la hauteur finale.
float fitToGround(std::vector<glm::vec3>& positions, const FitOptions& options);

} // namespace drapeo::io
