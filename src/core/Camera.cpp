#include "core/Camera.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>

namespace ne {

namespace {
constexpr glm::vec3 kWorldUp{0.0f, 1.0f, 0.0f};
}

void Camera::setPerspective(float fovYRadians, float aspect, float nearZ, float farZ) {
    projection_ = glm::perspective(fovYRadians, aspect, nearZ, farZ);
    projection_[1][1] *= -1.0f;  // GLM is OpenGL-handed; flip Y for Vulkan.
}

glm::vec3 Camera::front() const {
    float y = glm::radians(yaw);
    float p = glm::radians(pitch);
    return glm::normalize(glm::vec3(std::cos(y) * std::cos(p),
                                    std::sin(p),
                                    std::sin(y) * std::cos(p)));
}

glm::vec3 Camera::right() const {
    return glm::normalize(glm::cross(front(), kWorldUp));
}

glm::vec3 Camera::up() const {
    return glm::normalize(glm::cross(right(), front()));
}

void Camera::rotate(float deltaYawDeg, float deltaPitchDeg) {
    yaw += deltaYawDeg;
    pitch = std::clamp(pitch + deltaPitchDeg, -89.0f, 89.0f);
}

glm::mat4 Camera::view() const {
    return glm::lookAt(position, position + front(), kWorldUp);
}

} // namespace ne
