// geometry/Mannequin — mannequin de couture intégré (asset original de Drapeo),
// généré par programme : buste de révolution à section elliptique, tête,
// bras en pose A, jambes. Hauteur 1,75 m, pieds à y = 0, face vers +Z.
#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace drapeo::geometry {

void makeMannequin(std::vector<glm::vec3>& positions, std::vector<uint32_t>& indices);

} // namespace drapeo::geometry
