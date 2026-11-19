// rendering/Camera — caméra orbitale (rotation, zoom, panoramique)
#pragma once

#include <glm/glm.hpp>

namespace drapeo::rendering {

class OrbitCamera {
public:
    glm::vec3 target{0.0f, 0.6f, 0.0f};
    float yaw = 0.6f;        // radians autour de Y
    float pitch = 0.35f;     // radians au-dessus de l'horizon
    float distance = 3.2f;   // mètres
    float fovY = 45.0f;      // degrés

    void orbit(float dYaw, float dPitch);
    void zoom(float factor);
    void pan(float dx, float dy);

    [[nodiscard]] glm::vec3 position() const;
    [[nodiscard]] glm::mat4 view() const;
    [[nodiscard]] glm::mat4 projection(float aspect) const;

    /// Rayon monde passant par un point écran (coordonnées normalisées -1..1).
    void ray(float ndcX, float ndcY, float aspect, glm::vec3& origin, glm::vec3& direction) const;
};

} // namespace drapeo::rendering
