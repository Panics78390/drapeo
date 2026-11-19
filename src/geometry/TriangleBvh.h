// =============================================================================
//  geometry/TriangleBvh — hiérarchie de boîtes englobantes sur des triangles
//
//  Sert aux collisions tissu/avatar : pour chaque particule on cherche le point
//  le plus proche de la surface du corps dans un petit rayon. Avec une limite
//  de distance, l'élagage est très efficace : une particule loin du corps ne
//  visite que quelques nœuds.
//
//  Construction : découpage médian sur l'axe le plus long, feuilles ≤ 4 triangles.
// =============================================================================
#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace drapeo::geometry {

struct ClosestHit {
    glm::vec3 point{0.0f};    // point le plus proche sur la surface
    glm::vec3 normal{0.0f};   // normale du triangle touché (orientée vers l'extérieur)
    float distance = 0.0f;    // distance non signée
    uint32_t triangle = 0;
};

class TriangleBvh {
public:
    void build(const std::vector<glm::vec3>& positions, const std::vector<uint32_t>& indices);

    /// Point de la surface le plus proche de `p` à moins de `maxDistance`.
    bool closestPoint(const glm::vec3& p, float maxDistance, ClosestHit& hit) const;

    [[nodiscard]] bool empty() const { return nodes_.empty(); }
    [[nodiscard]] std::size_t triangleCount() const { return triangles_.size(); }

private:
    struct Node {
        glm::vec3 lo, hi;
        uint32_t first = 0;   // feuille : premier triangle ; nœud interne : enfant gauche
        uint32_t count = 0;   // 0 = nœud interne (enfant droit = first + 1 n'est pas garanti, voir right)
        uint32_t right = 0;
    };
    struct Tri {
        glm::vec3 a, b, c, n;
        uint32_t original;
    };

    uint32_t buildRecursive(uint32_t first, uint32_t count, std::vector<glm::vec3>& centroids);

    std::vector<Node> nodes_;
    std::vector<Tri> triangles_;
};

/// Point le plus proche de `p` sur le triangle abc (Ericson, Real-Time Collision Detection §5.1.5).
glm::vec3 closestPointOnTriangle(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c);

} // namespace drapeo::geometry
