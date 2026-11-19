#include "geometry/ClothMesh.h"

#include <cmath>
#include <numbers>
#include <unordered_map>

namespace drapeo::geometry {

ClothMesh makeRectangle(float width, float depth, int nx, int ny, glm::vec3 center)
{
    ClothMesh m;
    const int vx = nx + 1;
    const int vy = ny + 1;
    m.positions.reserve(static_cast<std::size_t>(vx * vy));
    m.uvs.reserve(m.positions.capacity());

    for (int j = 0; j < vy; ++j) {
        for (int i = 0; i < vx; ++i) {
            const float u = width * (static_cast<float>(i) / nx);
            const float v = depth * (static_cast<float>(j) / ny);
            m.positions.emplace_back(center.x - width * 0.5f + u, center.y, center.z - depth * 0.5f + v);
            m.uvs.emplace_back(u, v);
        }
    }

    m.indices.reserve(static_cast<std::size_t>(nx * ny * 6));
    auto id = [vx](int i, int j) { return static_cast<uint32_t>(j * vx + i); };
    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            const uint32_t a = id(i, j), b = id(i + 1, j), c = id(i, j + 1), d = id(i + 1, j + 1);
            // Orientation (normale vers +Y) et alternance des diagonales.
            if ((i + j) % 2 == 0) {
                m.indices.insert(m.indices.end(), {a, c, b, b, c, d});
            } else {
                m.indices.insert(m.indices.end(), {a, c, d, a, d, b});
            }
        }
    }
    buildTopology(m);
    return m;
}

void buildTopology(ClothMesh& mesh)
{
    mesh.edges.clear();
    mesh.bendPairs.clear();

    // clé d'arête (min,max) -> sommet opposé du premier triangle rencontré
    std::unordered_map<uint64_t, uint32_t> firstOpposite;
    firstOpposite.reserve(mesh.indices.size());

    const std::size_t triCount = mesh.indices.size() / 3;
    for (std::size_t t = 0; t < triCount; ++t) {
        const uint32_t tri[3] = {mesh.indices[3 * t], mesh.indices[3 * t + 1], mesh.indices[3 * t + 2]};
        for (int e = 0; e < 3; ++e) {
            const uint32_t a = tri[e], b = tri[(e + 1) % 3], opp = tri[(e + 2) % 3];
            const uint32_t lo = a < b ? a : b, hi = a < b ? b : a;
            const uint64_t key = (static_cast<uint64_t>(lo) << 32) | hi;
            auto [it, inserted] = firstOpposite.try_emplace(key, opp);
            if (inserted) {
                mesh.edges.push_back({lo, hi});
            } else {
                // Arête intérieure partagée par deux triangles : contrainte de flexion
                // entre les deux sommets opposés (formulation « distance » de Müller).
                mesh.bendPairs.push_back({it->second, opp});
            }
        }
    }
}

void computeVertexNormals(const std::vector<glm::vec3>& positions,
                          const std::vector<uint32_t>& indices,
                          std::vector<glm::vec3>& outNormals)
{
    outNormals.assign(positions.size(), glm::vec3(0.0f));
    for (std::size_t t = 0; t + 2 < indices.size(); t += 3) {
        const uint32_t a = indices[t], b = indices[t + 1], c = indices[t + 2];
        // Le produit vectoriel non normalisé est proportionnel à l'aire : pondération gratuite.
        const glm::vec3 n = glm::cross(positions[b] - positions[a], positions[c] - positions[a]);
        outNormals[a] += n;
        outNormals[b] += n;
        outNormals[c] += n;
    }
    for (auto& n : outNormals) {
        const float len = glm::length(n);
        n = len > 1e-12f ? n / len : glm::vec3(0, 1, 0);
    }
}

void makeUvSphere(int stacks, int slices, std::vector<glm::vec3>& outPositions, std::vector<uint32_t>& outIndices)
{
    outPositions.clear();
    outIndices.clear();
    const float pi = std::numbers::pi_v<float>;
    for (int s = 0; s <= stacks; ++s) {
        const float phi = pi * static_cast<float>(s) / stacks;
        for (int l = 0; l <= slices; ++l) {
            const float theta = 2.0f * pi * static_cast<float>(l) / slices;
            outPositions.emplace_back(std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta));
        }
    }
    const int row = slices + 1;
    for (int s = 0; s < stacks; ++s) {
        for (int l = 0; l < slices; ++l) {
            const auto a = static_cast<uint32_t>(s * row + l);
            const auto b = static_cast<uint32_t>((s + 1) * row + l);
            outIndices.insert(outIndices.end(), {a, a + 1, b, a + 1, b + 1, b});
        }
    }
}

} // namespace drapeo::geometry
