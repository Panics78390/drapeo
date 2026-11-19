#include "rendering/Camera.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

namespace drapeo::rendering {

void OrbitCamera::orbit(float dYaw, float dPitch)
{
    yaw += dYaw;
    pitch = std::clamp(pitch + dPitch, -1.5f, 1.5f);
}

void OrbitCamera::zoom(float factor) { distance = std::clamp(distance * factor, 0.3f, 30.0f); }

void OrbitCamera::pan(float dx, float dy)
{
    const glm::mat4 v = view();
    const glm::vec3 right(v[0][0], v[1][0], v[2][0]);
    const glm::vec3 up(v[0][1], v[1][1], v[2][1]);
    target += (-right * dx + up * dy) * distance;
}

glm::vec3 OrbitCamera::position() const
{
    return target + distance * glm::vec3(std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw));
}

glm::mat4 OrbitCamera::view() const { return glm::lookAt(position(), target, glm::vec3(0, 1, 0)); }

glm::mat4 OrbitCamera::projection(float aspect) const
{
    return glm::perspective(glm::radians(fovY), aspect, 0.01f, 100.0f);
}

void OrbitCamera::ray(float ndcX, float ndcY, float aspect, glm::vec3& origin, glm::vec3& direction) const
{
    const glm::mat4 inv = glm::inverse(projection(aspect) * view());
    glm::vec4 nearP = inv * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
    glm::vec4 farP = inv * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    nearP /= nearP.w;
    farP /= farP.w;
    origin = glm::vec3(nearP);
    direction = glm::normalize(glm::vec3(farP - nearP));
}

} // namespace drapeo::rendering
