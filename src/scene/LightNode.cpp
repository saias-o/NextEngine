#include "scene/LightNode.hpp"
#include "scene/SerializationHelpers.hpp"
#include <nlohmann/json.hpp>

namespace ne {

void LightNode::serialize(nlohmann::json& j, ResourceManager& resources) const {
    Node::serialize(j, resources);
    j["lightType"] = static_cast<int>(type);
    j["color"] = vec3ToJson(color);
    j["intensity"] = intensity;
    j["direction"] = vec3ToJson(direction);
    j["range"] = range;
    j["spotInnerAngle"] = spotInnerAngle;
    j["spotOuterAngle"] = spotOuterAngle;
    j["castShadows"] = castShadows;
    j["bakeMode"] = static_cast<int>(bakeMode);
}

void LightNode::deserialize(const nlohmann::json& j, ResourceManager& resources) {
    Node::deserialize(j, resources);
    if (j.contains("lightType")) type = static_cast<LightType>(j["lightType"].get<int>());
    if (j.contains("color")) color = jsonToVec3(j["color"], glm::vec3(1.0f));
    if (j.contains("intensity")) intensity = j["intensity"].get<float>();
    if (j.contains("direction")) direction = jsonToVec3(j["direction"], glm::vec3(0.0f, -1.0f, 0.0f));
    if (j.contains("range")) range = j["range"].get<float>();
    if (j.contains("spotInnerAngle")) spotInnerAngle = j["spotInnerAngle"].get<float>();
    if (j.contains("spotOuterAngle")) spotOuterAngle = j["spotOuterAngle"].get<float>();
    if (j.contains("castShadows")) castShadows = j["castShadows"].get<bool>();
    if (j.contains("bakeMode")) bakeMode = static_cast<LightBakeMode>(j["bakeMode"].get<int>());
}

} // namespace ne
