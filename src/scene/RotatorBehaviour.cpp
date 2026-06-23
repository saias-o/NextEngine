#include "scene/RotatorBehaviour.hpp"

#include "scene/Node.hpp"

#include <nlohmann/json.hpp>
#include <glm/gtc/quaternion.hpp>

namespace ne {

void RotatorBehaviour::onUpdate(float dt) {
    if (speed == 0.0f) return;
    if (glm::length(axis) < 1e-6f) return;

    const glm::vec3 a = glm::normalize(axis);
    const glm::quat delta = glm::angleAxis(glm::radians(speed) * dt, a);
    Transform& t = node()->transform();
    t.rotation = glm::normalize(delta * t.rotation);
}

void RotatorBehaviour::save(nlohmann::json& json) const {
    json["axis"] = {axis.x, axis.y, axis.z};
    json["speed"] = speed;
}

void RotatorBehaviour::load(const nlohmann::json& json) {
    if (json.contains("axis") && json["axis"].is_array() && json["axis"].size() == 3) {
        axis = {json["axis"][0].get<float>(),
                json["axis"][1].get<float>(),
                json["axis"][2].get<float>()};
    }
    if (json.contains("speed")) speed = json["speed"].get<float>();
}

} // namespace ne
