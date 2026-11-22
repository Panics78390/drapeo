// =============================================================================
//  geometry/BodyShape — mensurations, tailles et pose du mannequin intégré
//
//  Le maillage de repos (1,75 m) n'est pas régénéré : on déplace ses sommets.
//  La topologie, et donc le coût du rendu, ne change pas. La pose n'est calculée
//  que lorsqu'une animation est demandée.
// =============================================================================
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <glm/glm.hpp>

namespace drapeo::geometry {

struct BodyMeasures {
    float heightM = 1.75f;
    float chestCm = 90.0f;
    float waistCm = 70.0f;
    float hipsCm = 96.0f;
};

struct SizePreset {
    int size = 38;   // confection française, de 34 à 46
    BodyMeasures measures;
};

/// Tailles 34 à 46 (tour de poitrine, taille, hanches, et une stature associée).
[[nodiscard]] std::span<const SizePreset> sizePresets();
[[nodiscard]] const SizePreset* findSizePreset(int size);

/// Tours mesurés sur un mannequin (mètres), pieds à y = 0.
struct BodyBands {
    float height = 1.75f;
    float chestY = 1.3f, waistY = 1.05f, hipsY = 0.92f;
    float chest = 0.9f, waist = 0.7f, hips = 0.96f;
};

[[nodiscard]] BodyBands measureBands(const std::vector<glm::vec3>& rest);

enum class BodyRegion : std::uint8_t { Torso = 0, Head, ArmL, ArmR, LegL, LegR };

enum class BodyPose { Still, Walk, ArmsUp };

/// Classement des sommets du mannequin de repos (une fois, pas à chaque image).
struct BodyRig {
    std::vector<BodyRegion> region;
    int shoulderL = -1, shoulderR = -1, hipL = -1, hipR = -1;

    void build(const std::vector<glm::vec3>& rest);
    /// `shaped` a la même topologie que le repos. `time` est en secondes.
    void pose(const std::vector<glm::vec3>& shaped, BodyPose pose, float time, std::vector<glm::vec3>& out) const;
};

/// Déforme le buste (tours de poitrine, taille, hanches). La stature est appliquée ensuite
/// par fitToGround. Les membres gardent leur forme et suivent l'épaule ou la hanche.
void shapeBody(const std::vector<glm::vec3>& rest, const BodyRig& rig, const BodyBands& reference,
               const BodyMeasures& target, std::vector<glm::vec3>& out);

} // namespace drapeo::geometry
