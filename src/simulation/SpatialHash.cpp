#include "simulation/SpatialHash.h"

#include <cmath>

namespace drapeo::simulation {

SpatialHash::SpatialHash(float cellSize, std::size_t maxObjects)
    : cellSize_(cellSize), tableSize_(2 * maxObjects + 1), cellStart_(tableSize_ + 1, 0), cellEntries_(maxObjects, 0)
{
}

int SpatialHash::cellCoord(float v) const
{
    // floor rapide (std::floor est étonnamment coûteux dans cette boucle chaude)
    const float q = v / cellSize_;
    const int i = static_cast<int>(q);
    return i - (q < static_cast<float>(i) ? 1 : 0);
}

std::size_t SpatialHash::hashCoords(int xi, int yi, int zi) const
{
    // Hachage classique par grands nombres premiers (Teschner et al. 2003).
    // Arithmétique non signée : le débordement est défini (pas d'UB).
    const uint32_t h = (static_cast<uint32_t>(xi) * 92837111u) ^ (static_cast<uint32_t>(yi) * 689287499u) ^
                       (static_cast<uint32_t>(zi) * 283923481u);
    return h % tableSize_;
}

void SpatialHash::build(const std::vector<glm::vec3>& points)
{
    if (points.size() > cellEntries_.size()) {
        tableSize_ = 2 * points.size() + 1;
        cellEntries_.resize(points.size());
    }
    cellStart_.assign(tableSize_ + 1, 0);

    for (const auto& p : points) {
        ++cellStart_[hashCoords(cellCoord(p.x), cellCoord(p.y), cellCoord(p.z))];
    }
    // Sommes préfixes : cellStart_[h] = fin de la cellule h
    uint32_t start = 0;
    for (std::size_t i = 0; i < tableSize_; ++i) {
        start += cellStart_[i];
        cellStart_[i] = start;
    }
    cellStart_[tableSize_] = start;
    // Remplissage à rebours : cellStart_[h] devient le début de la cellule h
    for (std::size_t i = 0; i < points.size(); ++i) {
        const auto& p = points[i];
        const std::size_t h = hashCoords(cellCoord(p.x), cellCoord(p.y), cellCoord(p.z));
        cellEntries_[--cellStart_[h]] = static_cast<uint32_t>(i);
    }
}

void SpatialHash::query(const glm::vec3& p, float radius, std::vector<uint32_t>& out) const
{
    const int x0 = cellCoord(p.x - radius), x1 = cellCoord(p.x + radius);
    const int y0 = cellCoord(p.y - radius), y1 = cellCoord(p.y + radius);
    const int z0 = cellCoord(p.z - radius), z1 = cellCoord(p.z + radius);
    for (int xi = x0; xi <= x1; ++xi)
        for (int yi = y0; yi <= y1; ++yi)
            for (int zi = z0; zi <= z1; ++zi) {
                const std::size_t h = hashCoords(xi, yi, zi);
                for (uint32_t k = cellStart_[h]; k < cellStart_[h + 1]; ++k) out.push_back(cellEntries_[k]);
            }
}

} // namespace drapeo::simulation
