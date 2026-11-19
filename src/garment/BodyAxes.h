// =============================================================================
//  garment/BodyAxes — axes du buste, des bras et des jambes d'un avatar
//
//  Les pièces sont enroulées sur des cylindres le long de ces axes. Pour le
//  mannequin intégré ce sont des valeurs fixes ; pour un avatar importé (FBX,
//  OBJ) on les estime à partir de ses sommets : les bras (ACP des sommets
//  éloignés du buste), les jambes (centres de tranches horizontales).
//  Coordonnées ramenées à un avatar de 1,75 m (multipliées par bodyScale).
// =============================================================================
#pragma once

#include <vector>

#include <glm/glm.hpp>

namespace drapeo::garment {

struct BodyAxes {
    glm::vec3 torsoCenter{0.0f};                        // centre du buste (x, z ; y ignoré)
    glm::vec3 armTop[2]{{0.175f, 1.40f, 0.0f}, {-0.175f, 1.40f, 0.0f}};   // [0] gauche (+X), [1] droite
    glm::vec3 armBottom[2]{{0.465f, 0.96f, 0.02f}, {-0.465f, 0.96f, 0.02f}};
    glm::vec3 legTop[2]{{0.095f, 0.98f, 0.0f}, {-0.095f, 0.98f, 0.0f}};
    glm::vec3 legBottom[2]{{0.10f, 0.04f, 0.0f}, {-0.10f, 0.04f, 0.0f}};
};

/// Estime les axes d'un avatar debout (pieds à y = 0, centré en x / z), bras
/// écartés (pose en A ou en T). `positions` en mètres, à sa taille réelle.
/// Renvoie les axes du mannequin si l'estimation échoue.
BodyAxes estimateBodyAxes(const std::vector<glm::vec3>& positions);

} // namespace drapeo::garment
