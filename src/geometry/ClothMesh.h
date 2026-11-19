// =============================================================================
//  geometry/ClothMesh — maillage triangulaire d'une pièce de tissu
//
//  Un ClothMesh est indépendant de sa provenance : aujourd'hui une grille
//  rectangulaire, demain un patron 2D triangulé par CDT. La topologie (arêtes,
//  paires de flexion) est déduite des triangles, donc le solveur fonctionne
//  pour n'importe quelle triangulation.
// =============================================================================
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace drapeo::geometry {

struct ClothMesh {
    std::vector<glm::vec3> positions;   // positions de repos (mètres)
    std::vector<glm::vec2> uvs;         // coordonnées 2D du patron (mètres) — servent aussi d'UV
    std::vector<uint32_t>  indices;     // 3 indices par triangle

    // Topologie dérivée (remplie par buildTopology)
    std::vector<std::array<uint32_t, 2>> edges;      // arêtes uniques  -> contraintes d'étirement/cisaillement
    std::vector<std::array<uint32_t, 2>> bendPairs;  // sommets opposés de 2 triangles adjacents -> flexion

    [[nodiscard]] std::size_t vertexCount()   const { return positions.size(); }
    [[nodiscard]] std::size_t triangleCount() const { return indices.size() / 3; }
};

/// Grille rectangulaire horizontale (plan XZ) centrée sur `center`.
/// nx, ny = nombre de cellules. Les diagonales alternent (motif « union jack »)
/// pour éviter un comportement anisotrope du tissu.
[[nodiscard]] ClothMesh makeRectangle(float width, float depth, int nx, int ny, glm::vec3 center);

/// Calcule `edges` et `bendPairs` à partir de `indices`.
void buildTopology(ClothMesh& mesh);

/// Normales de sommets pondérées par l'aire des triangles.
void computeVertexNormals(const std::vector<glm::vec3>& positions,
                          const std::vector<uint32_t>& indices,
                          std::vector<glm::vec3>& outNormals);

/// Sphère UV (rendu et collisions de test).
void makeUvSphere(int stacks, int slices,
                  std::vector<glm::vec3>& outPositions,
                  std::vector<uint32_t>& outIndices);

} // namespace drapeo::geometry
