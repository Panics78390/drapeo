#include "simulation/Materials.h"

#include <array>

namespace drapeo::simulation {

std::span<const FabricMaterial> fabricPresets()
{
    // Valeurs choisies pour une résolution d'environ 2 cm. Grammages indicatifs :
    // soie ~60 g/m², jersey ~180, coton ~150, lin ~180, jean ~400, cuir ~900.
    // La compliance est l'inverse d'une raideur : le jersey tombe, le jean tient.
    static const std::array<FabricMaterial, 6> presets{{
        {"Coton",  0.0f,    2.0e-1f, 0.15f, 0.006f, 0.5f, 20.0f, 0.45f, {0.86f, 0.84f, 0.78f}, 0.85f, 0.35f},
        {"Jersey", 2.0e-6f, 1.2f,    0.18f, 0.006f, 0.35f, 12.0f, 0.40f, {0.55f, 0.62f, 0.72f}, 0.72f, 0.45f},
        {"Soie",   1.0e-7f, 5.0f,    0.06f, 0.004f, 0.2f, 8.0f, 0.20f, {0.70f, 0.12f, 0.22f}, 0.35f, 0.80f},
        {"Jean",   0.0f,    1.0e-2f, 0.40f, 0.008f, 0.8f, 30.0f, 0.60f, {0.18f, 0.28f, 0.48f}, 0.90f, 0.20f},
        {"Lin",    0.0f,    8.0e-2f, 0.18f, 0.007f, 0.45f, 16.0f, 0.42f, {0.78f, 0.74f, 0.62f}, 0.88f, 0.22f},
        {"Cuir",   0.0f,    5.0e-4f, 0.90f, 0.010f, 1.5f, 50.0f, 0.70f, {0.36f, 0.20f, 0.12f}, 0.55f, 0.05f},
    }};
    return presets;
}

} // namespace drapeo::simulation
