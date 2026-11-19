// =============================================================================
//  simulation/Materials — préréglages de tissus
//
//  Les compliances XPBD (α, en m/N) sont l'inverse d'une rigidité : 0 = rigide,
//  plus grand = plus souple. Elles sont indépendantes du pas de temps, ce qui
//  permet de changer le nombre de sous-pas sans re-régler le tissu.
// =============================================================================
#pragma once

#include <span>
#include <string>

#include <glm/glm.hpp>

namespace drapeo::simulation {

struct FabricMaterial {
    std::string name;
    float stretchCompliance = 0.0f;   // arêtes du maillage (étirement + cisaillement via les diagonales)
    float bendCompliance    = 1.0f;   // paires de flexion
    float density           = 0.15f;  // kg/m² (grammage 150 g/m²)
    float thickness         = 0.006f; // m — distance de contact avec les obstacles
    float damping           = 0.5f;   // amortissement visqueux (1/s) : v *= exp(-damping·dt)
    float internalDamping   = 20.0f;  // amortissement interne (1/s) : lisse les vitesses voisines
    float friction          = 0.4f;   // coefficient de frottement avec les obstacles (0..1)
    glm::vec3 color{0.8f};            // couleur de base (sRGB)
    float roughness         = 0.8f;   // PBR
    float sheen             = 0.3f;   // reflet rasant typique des fibres
};

/// Coton, soie, denim, cuir.
[[nodiscard]] std::span<const FabricMaterial> fabricPresets();

} // namespace drapeo::simulation
