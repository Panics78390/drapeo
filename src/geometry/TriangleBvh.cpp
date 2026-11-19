#include "geometry/TriangleBvh.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace drapeo::geometry {

glm::vec3 closestPointOnTriangle(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c)
{
    const glm::vec3 ab = b - a, ac = c - a, ap = p - a;
    const float d1 = glm::dot(ab, ap), d2 = glm::dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) return a;

    const glm::vec3 bp = p - b;
    const float d3 = glm::dot(ab, bp), d4 = glm::dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) return b;

    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) return a + ab * (d1 / (d1 - d3));

    const glm::vec3 cp = p - c;
    const float d5 = glm::dot(ab, cp), d6 = glm::dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) return c;

    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) return a + ac * (d2 / (d2 - d6));

    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
        return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));

    const float denom = 1.0f / (va + vb + vc);
    return a + ab * (vb * denom) + ac * (vc * denom);
}

void TriangleBvh::build(const std::vector<glm::vec3>& positions, const std::vector<uint32_t>& indices)
{
    nodes_.clear();
    triangles_.clear();
    const std::size_t count = indices.size() / 3;
    triangles_.reserve(count);
    std::vector<glm::vec3> centroids;
    centroids.reserve(count);
    for (std::size_t t = 0; t < count; ++t) {
        const glm::vec3 a = positions[indices[3 * t]], b = positions[indices[3 * t + 1]], c = positions[indices[3 * t + 2]];
        const glm::vec3 n = glm::cross(b - a, c - a);
        const float len = glm::length(n);
        if (len < 1e-12f) continue;  // triangle dégénéré
        triangles_.push_back({a, b, c, n / len, static_cast<uint32_t>(t)});
        centroids.push_back((a + b + c) / 3.0f);
    }
    if (triangles_.empty()) return;
    nodes_.reserve(2 * triangles_.size() / 4 + 8);
    buildRecursive(0, static_cast<uint32_t>(triangles_.size()), centroids);
}

uint32_t TriangleBvh::buildRecursive(uint32_t first, uint32_t count, std::vector<glm::vec3>& centroids)
{
    Node node;
    node.lo = glm::vec3(1e30f);
    node.hi = glm::vec3(-1e30f);
    glm::vec3 clo(1e30f), chi(-1e30f);
    for (uint32_t i = first; i < first + count; ++i) {
        const Tri& t = triangles_[i];
        node.lo = glm::min(node.lo, glm::min(t.a, glm::min(t.b, t.c)));
        node.hi = glm::max(node.hi, glm::max(t.a, glm::max(t.b, t.c)));
        clo = glm::min(clo, centroids[i]);
        chi = glm::max(chi, centroids[i]);
    }
    const auto index = static_cast<uint32_t>(nodes_.size());
    nodes_.push_back(node);

    if (count <= 4) {
        nodes_[index].first = first;
        nodes_[index].count = count;
        return index;
    }

    const glm::vec3 extent = chi - clo;
    const int axis = (extent.x > extent.y && extent.x > extent.z) ? 0 : (extent.y > extent.z ? 1 : 2);
    const uint32_t mid = first + count / 2;

    // Tri partiel des triangles (et de leurs centroïdes) autour de la médiane.
    std::vector<uint32_t> order(count);
    std::iota(order.begin(), order.end(), first);
    std::nth_element(order.begin(), order.begin() + (mid - first), order.end(),
                     [&](uint32_t l, uint32_t r) { return centroids[l][axis] < centroids[r][axis]; });
    std::vector<Tri> tris(count);
    std::vector<glm::vec3> cents(count);
    for (uint32_t k = 0; k < count; ++k) {
        tris[k] = triangles_[order[k]];
        cents[k] = centroids[order[k]];
    }
    std::copy(tris.begin(), tris.end(), triangles_.begin() + first);
    std::copy(cents.begin(), cents.end(), centroids.begin() + first);

    const uint32_t left = buildRecursive(first, mid - first, centroids);
    const uint32_t right = buildRecursive(mid, first + count - mid, centroids);
    nodes_[index].first = left;
    nodes_[index].right = right;
    nodes_[index].count = 0;
    return index;
}

namespace {
float distanceSqToBox(const glm::vec3& p, const glm::vec3& lo, const glm::vec3& hi)
{
    const glm::vec3 d = glm::max(glm::max(lo - p, p - hi), glm::vec3(0.0f));
    return glm::dot(d, d);
}
} // namespace

bool TriangleBvh::closestPoint(const glm::vec3& p, float maxDistance, ClosestHit& hit) const
{
    if (nodes_.empty()) return false;
    float best = maxDistance * maxDistance;
    bool found = false;
    uint32_t stack[64];
    int top = 0;
    stack[top++] = 0;
    while (top > 0) {
        const Node& node = nodes_[stack[--top]];
        if (distanceSqToBox(p, node.lo, node.hi) > best) continue;
        if (node.count > 0) {
            for (uint32_t i = node.first; i < node.first + node.count; ++i) {
                const Tri& t = triangles_[i];
                const glm::vec3 q = closestPointOnTriangle(p, t.a, t.b, t.c);
                const glm::vec3 d = p - q;
                const float dsq = glm::dot(d, d);
                if (dsq < best) {
                    best = dsq;
                    hit.point = q;
                    hit.normal = t.n;
                    hit.triangle = t.original;
                    found = true;
                }
            }
        } else if (top + 2 <= 64) {
            // Visiter d'abord l'enfant le plus proche : élagage plus rapide.
            const Node& l = nodes_[node.first];
            const Node& r = nodes_[node.right];
            const float dl = distanceSqToBox(p, l.lo, l.hi), dr = distanceSqToBox(p, r.lo, r.hi);
            if (dl < dr) {
                stack[top++] = node.right;
                stack[top++] = node.first;
            } else {
                stack[top++] = node.first;
                stack[top++] = node.right;
            }
        }
    }
    if (found) hit.distance = std::sqrt(best);
    return found;
}

} // namespace drapeo::geometry
