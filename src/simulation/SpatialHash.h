// =============================================================================
//  simulation/SpatialHash — grille de hachage dense pour les auto-collisions
//
//  Construction en O(n) par tri par comptage, requête « voisins dans un rayon »
//  en O(k). Approche décrite par M. Müller (Ten Minute Physics, 2022).
// =============================================================================
#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace drapeo::simulation {

class SpatialHash {
public:
    SpatialHash(float cellSize, std::size_t maxObjects);

    void build(const std::vector<glm::vec3>& points);

    /// Ajoute dans `out` les indices des points à moins de `radius` de `p`
    /// (candidats : à filtrer ensuite par distance exacte).
    void query(const glm::vec3& p, float radius, std::vector<uint32_t>& out) const;

    [[nodiscard]] float cellSize() const { return cellSize_; }

private:
    [[nodiscard]] int cellCoord(float v) const;
    [[nodiscard]] std::size_t hashCoords(int xi, int yi, int zi) const;

    float cellSize_;
    std::size_t tableSize_;
    std::vector<uint32_t> cellStart_;    // tableSize + 1
    std::vector<uint32_t> cellEntries_;  // indices des points triés par cellule
};

} // namespace drapeo::simulation
