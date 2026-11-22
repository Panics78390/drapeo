#include "geometry/BodyShape.h"

#include <algorithm>
#include <cmath>
#include <array>

namespace drapeo::geometry {

namespace {
constexpr float kPi = 3.14159265f;

float ellipseCircumference(float a, float b)
{
    a = std::max(a, 1e-4f);
    b = std::max(b, 1e-4f);
    // Ramanujan : assez précis pour comparer deux tours.
    return kPi * (3.0f * (a + b) - std::sqrt((3.0f * a + b) * (a + 3.0f * b)));
}

float bandRadius(const std::vector<glm::vec3>& pts, float y, float half, bool axisX)
{
    float m = 0.0f;
    for (const auto& p : pts) {
        if (std::abs(p.y - y) > half) continue;
        if (std::abs(p.x) > 0.195f || std::abs(p.z) > 0.16f) continue;   // le buste, pas les bras
        m = std::max(m, axisX ? std::abs(p.x) : std::abs(p.z));
    }
    return m;
}

float bump(float y, float center, float sigma)
{
    const float d = (y - center) / sigma;
    return std::exp(-d * d);
}
} // namespace

std::span<const SizePreset> sizePresets()
{
    // Tours en cm, stature indicative. Un cran de taille (2) ≈ +4 cm de tour.
    static const std::array<SizePreset, 7> presets{{
        {34, {1.60f, 82.0f, 62.0f, 88.0f}},
        {36, {1.63f, 86.0f, 66.0f, 92.0f}},
        {38, {1.66f, 90.0f, 70.0f, 96.0f}},
        {40, {1.68f, 94.0f, 74.0f, 100.0f}},
        {42, {1.70f, 98.0f, 78.0f, 104.0f}},
        {44, {1.72f, 104.0f, 84.0f, 110.0f}},
        {46, {1.74f, 110.0f, 90.0f, 116.0f}},
    }};
    return presets;
}

const SizePreset* findSizePreset(int size)
{
    for (const auto& p : sizePresets())
        if (p.size == size) return &p;
    return nullptr;
}

BodyBands measureBands(const std::vector<glm::vec3>& rest)
{
    BodyBands b;
    if (rest.empty()) return b;
    float h = 0.0f;
    for (const auto& p : rest) h = std::max(h, p.y);
    b.height = std::max(h, 0.2f);
    b.chestY = 0.75f * b.height;
    b.waistY = 0.62f * b.height;
    b.hipsY = 0.53f * b.height;
    const float half = 0.03f * b.height;
    auto circ = [&](float y) {
        return ellipseCircumference(bandRadius(rest, y, half, true), bandRadius(rest, y, half, false));
    };
    b.chest = circ(b.chestY);
    b.waist = circ(b.waistY);
    b.hips = circ(b.hipsY);
    return b;
}

void BodyRig::build(const std::vector<glm::vec3>& rest)
{
    region.assign(rest.size(), BodyRegion::Torso);
    shoulderL = shoulderR = hipL = hipR = -1;
    if (rest.empty()) return;
    float h = 0.0f;
    for (const auto& p : rest) h = std::max(h, p.y);
    h = std::max(h, 0.2f);
    float bestSL = 1e9f, bestSR = 1e9f, bestHL = 1e9f, bestHR = 1e9f;
    for (std::size_t i = 0; i < rest.size(); ++i) {
        const auto& p = rest[i];
        BodyRegion r = BodyRegion::Torso;
        if (p.y > 0.86f * h) r = BodyRegion::Head;
        else if (p.x < -0.20f && p.y > 0.50f * h) r = BodyRegion::ArmL;
        else if (p.x > 0.20f && p.y > 0.50f * h) r = BodyRegion::ArmR;
        else if (p.x < -0.05f && p.y < 0.56f * h) r = BodyRegion::LegL;
        else if (p.x > 0.05f && p.y < 0.56f * h) r = BodyRegion::LegR;
        region[i] = r;
        if (r == BodyRegion::ArmL && p.x < bestSL) { bestSL = p.x; shoulderL = static_cast<int>(i); }
        if (r == BodyRegion::ArmR && p.x < bestSR) { bestSR = p.x; shoulderR = static_cast<int>(i); }
        if (r == BodyRegion::LegL && p.y > bestHL) { bestHL = p.y; hipL = static_cast<int>(i); }
        if (r == BodyRegion::LegR && p.y > bestHR) { bestHR = p.y; hipR = static_cast<int>(i); }
    }
    // L'épaule gauche est le sommet de bras le plus proche du buste (x le moins négatif) :
    // on a pris le plus petit x. On recommence pour le plus proche de zéro.
    bestSL = -1e9f;
    bestSR = 1e9f;
    shoulderL = shoulderR = -1;
    for (std::size_t i = 0; i < rest.size(); ++i) {
        if (region[i] == BodyRegion::ArmL && rest[i].x > bestSL) { bestSL = rest[i].x; shoulderL = static_cast<int>(i); }
        if (region[i] == BodyRegion::ArmR && rest[i].x < bestSR) { bestSR = rest[i].x; shoulderR = static_cast<int>(i); }
    }
}

void shapeBody(const std::vector<glm::vec3>& rest, const BodyRig& rig, const BodyBands& reference,
               const BodyMeasures& target, std::vector<glm::vec3>& out)
{
    out.resize(rest.size());
    const float sc = reference.chest > 1e-3f ? (target.chestCm / 100.0f) / reference.chest : 1.0f;
    const float sw = reference.waist > 1e-3f ? (target.waistCm / 100.0f) / reference.waist : 1.0f;
    const float sh = reference.hips > 1e-3f ? (target.hipsCm / 100.0f) / reference.hips : 1.0f;
    auto scaleAt = [&](float y) {
        const float wc = bump(y, reference.chestY, 0.055f * reference.height);
        const float ww = bump(y, reference.waistY, 0.045f * reference.height);
        const float wh = bump(y, reference.hipsY, 0.055f * reference.height);
        const float sum = wc + ww + wh;
        if (sum < 0.04f) return 1.0f;
        return (wc * sc + ww * sw + wh * sh) / sum;
    };
    auto pivotOf = [&](int index) {
        if (index < 0 || index >= static_cast<int>(rest.size())) return glm::vec3(0.0f);
        const glm::vec3& p = rest[static_cast<std::size_t>(index)];
        const float s = scaleAt(p.y);
        return glm::vec3(p.x * s, p.y, p.z * s);
    };
    const glm::vec3 pivots[4] = {pivotOf(rig.shoulderL), pivotOf(rig.shoulderR), pivotOf(rig.hipL), pivotOf(rig.hipR)};
    const glm::vec3 rests[4] = {
        rig.shoulderL >= 0 ? rest[static_cast<std::size_t>(rig.shoulderL)] : glm::vec3(0.0f),
        rig.shoulderR >= 0 ? rest[static_cast<std::size_t>(rig.shoulderR)] : glm::vec3(0.0f),
        rig.hipL >= 0 ? rest[static_cast<std::size_t>(rig.hipL)] : glm::vec3(0.0f),
        rig.hipR >= 0 ? rest[static_cast<std::size_t>(rig.hipR)] : glm::vec3(0.0f)};

    for (std::size_t i = 0; i < rest.size(); ++i) {
        const glm::vec3& p = rest[i];
        const BodyRegion r = i < rig.region.size() ? rig.region[i] : BodyRegion::Torso;
        if (r == BodyRegion::Torso || r == BodyRegion::Head) {
            float s = scaleAt(p.y);
            if (r == BodyRegion::Head) s = 1.0f + (s - 1.0f) * 0.12f;
            out[i] = glm::vec3(p.x * s, p.y, p.z * s);
            continue;
        }
        const int k = r == BodyRegion::ArmL ? 0 : r == BodyRegion::ArmR ? 1 : r == BodyRegion::LegL ? 2 : 3;
        out[i] = pivots[k] + (p - rests[k]);
    }
}

void BodyRig::pose(const std::vector<glm::vec3>& shaped, BodyPose which, float time, std::vector<glm::vec3>& out) const
{
    out = shaped;
    if (which == BodyPose::Still || shaped.empty()) return;
    auto pivot = [&](int index) {
        if (index < 0 || index >= static_cast<int>(shaped.size())) return glm::vec3(0.0f);
        return shaped[static_cast<std::size_t>(index)];
    };
    const glm::vec3 piv[4] = {pivot(shoulderL), pivot(shoulderR), pivot(hipL), pivot(hipR)};
    const float swing = std::sin(time * 5.2f);
    auto rotX = [](glm::vec3 d, float a) {
        const float c = std::cos(a), s = std::sin(a);
        return glm::vec3(d.x, c * d.y - s * d.z, s * d.y + c * d.z);
    };
    auto rotZ = [](glm::vec3 d, float a) {
        const float c = std::cos(a), s = std::sin(a);
        return glm::vec3(c * d.x - s * d.y, s * d.x + c * d.y, d.z);
    };
    const float bob = which == BodyPose::Walk ? 0.008f * std::sin(time * 10.4f) : 0.0f;
    for (std::size_t i = 0; i < shaped.size(); ++i) {
        const BodyRegion r = i < region.size() ? region[i] : BodyRegion::Torso;
        glm::vec3 p = shaped[i];
        if (r == BodyRegion::ArmL || r == BodyRegion::ArmR || r == BodyRegion::LegL || r == BodyRegion::LegR) {
            const int k = r == BodyRegion::ArmL ? 0 : r == BodyRegion::ArmR ? 1 : r == BodyRegion::LegL ? 2 : 3;
            glm::vec3 d = p - piv[k];
            if (which == BodyPose::Walk) {
                float ang = swing * (k >= 2 ? 0.42f : -0.32f);
                if (k == 1 || k == 3) ang = -ang;
                d = rotX(d, ang);
            } else if (r == BodyRegion::ArmL || r == BodyRegion::ArmR) {
                // Rotation autour de Z : le signe relève chaque bras.
                const float ang = r == BodyRegion::ArmL ? -1.15f : 1.15f;
                d = rotZ(d, ang);
            }
            p = piv[k] + d;
        }
        p.y += bob;
        out[i] = p;
    }
}

} // namespace drapeo::geometry
