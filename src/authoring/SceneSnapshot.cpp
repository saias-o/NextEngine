#include "authoring/SceneSnapshot.hpp"

#include "core/FormatVersions.hpp"
#include "core/Reflection.hpp"
#include "graphics/ResourceManager.hpp"
#include "scene/LightNode.hpp"
#include "scene/Node.hpp"
#include "scene/ParticleSystemNode.hpp"
#include "scene/Scene.hpp"
#include "scene/SerializationHelpers.hpp"
#include "scene/WaterNode.hpp"

#include <nlohmann/json.hpp>

namespace saida::authoring {
namespace {

using json = nlohmann::json;

json transformToJson(const Transform& t) {
    return {
        {"position", vec3ToJson(t.position)},
        {"rotation", json::array({t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w})},
        {"scale", vec3ToJson(t.scale)},
    };
}

const reflect::TypeDesc* reflectedNodeDesc(const Node& n) {
    const std::string type = n.typeName() ? n.typeName() : "";
    if (type == LightNode::reflectName()) return &reflect::localDesc<LightNode>();
    if (type == WaterNode::reflectName()) return &reflect::localDesc<WaterNode>();
    if (type == ParticleSystemNode::reflectName()) return &reflect::localDesc<ParticleSystemNode>();
    return nullptr;
}

json serializeNodeWithoutResources(const Node& node) {
    json out;
    out["type"] = node.typeName();
    out["id"] = node.id();
    out["name"] = node.name();
    out["enabled"] = node.enabled();
    out["transform"] = transformToJson(node.transform());
    out["behaviours"] = json::array();

    if (!node.importedFromPath().empty())
        out["importedFrom"] = node.importedFromPath();
    if (!node.groups().empty())
        out["groups"] = node.groups();

    if (const reflect::TypeDesc* desc = reflectedNodeDesc(node))
        desc->saveTo(&node, out);

    json children = json::array();
    for (const auto& child : node.children())
        children.push_back(serializeNodeWithoutResources(*child));
    out["children"] = std::move(children);
    return out;
}

json sceneSettingsToJson(const Scene& scene) {
    const SceneSettings& s = scene.settings();
    return {
        {"ambient", vec3ToJson(s.ambientLight)},
        {"clearColor", vec3ToJson(s.clearColor)},
        {"postProcessing", s.enablePostProcessing},
        {"lightingMode", static_cast<int>(s.lightingMode)},
        {"giEnabled", s.giEnabled},
        {"giMode", static_cast<int>(s.giMode)},
        {"giIntensity", s.giIntensity},
        {"skyboxTexture", s.skyboxTexture},
        {"skyboxExposure", s.skyboxExposure},
        {"skyboxRotation", s.skyboxRotation},
        {"iblEnabled", s.iblEnabled},
        {"iblDiffuseIntensity", s.iblDiffuseIntensity},
        {"iblSpecularIntensity", s.iblSpecularIntensity},
        {"aoEnabled", s.aoEnabled},
        {"aoRadius", s.aoRadius},
        {"aoIntensity", s.aoIntensity},
        {"aoPower", s.aoPower},
        {"fogEnabled", s.fogEnabled},
        {"fogColor", vec3ToJson(glm::vec3(s.fogColor))},
        {"fogStart", s.fogStart},
        {"fogDensity", s.fogDensity},
        {"bloomEnabled", s.bloomEnabled},
        {"bloomThreshold", s.bloomThreshold},
        {"bloomIntensity", s.bloomIntensity},
        {"bloomRadius", s.bloomRadius},
        {"changeRenderingAtLoad", s.changeRenderingAtLoad},
    };
}

json serializeSceneWithoutResources(Scene& scene) {
    json root = serializeNodeWithoutResources(scene);
    root["settings"] = sceneSettingsToJson(scene);
    if (!scene.connections().empty()) {
        json conns = json::array();
        for (const auto& c : scene.connections())
            conns.push_back({{"from", c.from}, {"signal", c.signal},
                             {"to", c.to}, {"slot", c.slot}});
        root["connections"] = std::move(conns);
    }
    return root;
}

} // namespace

std::string serializeSceneSnapshot(Scene& scene, ResourceManager& resources) {
    json doc;
    doc["version"] = format::kSceneVersion;
    scene.serialize(doc["scene"], resources);
    return doc.dump(2);
}

std::string serializeSceneSnapshot(Scene& scene, ResourceManager* resources) {
    if (resources) return serializeSceneSnapshot(scene, *resources);

    json doc;
    doc["version"] = format::kSceneVersion;
    doc["scene"] = serializeSceneWithoutResources(scene);
    doc["snapshotMode"] = "authoring-headless";
    return doc.dump(2);
}

} // namespace saida::authoring
