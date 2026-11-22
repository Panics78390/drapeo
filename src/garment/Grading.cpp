#include "garment/Grading.h"

#include <cmath>
#include <string>

namespace drapeo::garment {

namespace {
glm::vec2 pieceCentroid(const Panel& p)
{
    if (p.points.empty()) return {};
    glm::vec2 c(0.0f);
    for (const auto& q : p.points) c += q;
    return c / static_cast<float>(p.points.size());
}
} // namespace

int appendGradedSize(Garment& g, int baseSize, int targetSize)
{
    const int n = static_cast<int>(g.panels.size());
    if (n == 0 || targetSize == baseSize) return 0;
    const float steps = static_cast<float>(targetSize - baseSize) / 2.0f;
    const float width = std::pow(1.04f, steps);
    const float length = std::pow(1.008f, steps);

    float srcMaxX = -1e9f;
    for (const auto& p : g.panels)
        for (const auto& q : p.points) srcMaxX = std::max(srcMaxX, q.x);

    std::vector<Panel> copies;
    copies.reserve(static_cast<std::size_t>(n));
    float minX = 1e9f;
    for (const auto& src : g.panels) {
        Panel p = src;
        p.name += " " + std::to_string(targetSize);
        p.active3D = false;
        p.linkGroup = 0;
        const glm::vec2 c = pieceCentroid(src);
        for (auto& q : p.points) q = c + glm::vec2((q.x - c.x) * width, (q.y - c.y) * length);
        for (const auto& q : p.points) minX = std::min(minX, q.x);
        copies.push_back(std::move(p));
    }
    const float shift = srcMaxX + 0.04f - minX;
    for (auto& p : copies)
        for (auto& q : p.points) q.x += shift;

    PieceGroup group;
    group.name = "Taille " + std::to_string(targetSize);
    const int groupIndex = static_cast<int>(g.groups.size());
    g.groups.push_back(group);
    for (auto& p : copies) p.group = groupIndex;

    const int seamN = static_cast<int>(g.seams.size());
    for (int i = 0; i < seamN; ++i) {
        Seam s = g.seams[static_cast<std::size_t>(i)];
        s.a.panel += n;
        s.b.panel += n;
        g.seams.push_back(s);
    }
    const int attN = static_cast<int>(g.attachments.size());
    for (int i = 0; i < attN; ++i) {
        Attachment a = g.attachments[static_cast<std::size_t>(i)];
        a.patch += n;
        a.base += n;
        g.attachments.push_back(a);
    }
    g.panels.insert(g.panels.end(), copies.begin(), copies.end());
    return n;
}

} // namespace drapeo::garment
