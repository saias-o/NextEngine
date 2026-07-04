#include "authoring/SceneSnapshot.hpp"

#include "core/FormatVersions.hpp"
#include "core/Reflection.hpp"
#include "graphics/ResourceManager.hpp"
#include "scene/Behaviour.hpp"
#include "scene/BehaviourRegistry.hpp"
#include "scene/LightNode.hpp"
#include "scene/MeshNode.hpp"
#include "scene/Node.hpp"
#include "scene/ParticleSystemNode.hpp"
#include "scene/ReflectedTypes.hpp"
#include "scene/Scene.hpp"
#include "scene/SerializationHelpers.hpp"
#include "scene/WaterNode.hpp"

#include <nlohmann/json.hpp>

#include <glm/gtc/quaternion.hpp>

#include <memory>

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

    // Behaviours (gameplay logic) — same {type, enabled, ...reflected props}
    // shape as the full serializer, so the headless snapshot carries logic too.
    json behavioursJson = json::array();
    for (const auto& b : node.behaviours()) {
        if (const char* tn = b->typeName()) {
            json bj;
            bj["type"] = tn;
            bj["enabled"] = b->enabled();
            b->save(bj);
            behavioursJson.push_back(std::move(bj));
        }
    }
    out["behaviours"] = std::move(behavioursJson);

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

// --- headless deserialization (inverse of serializeSceneWithoutResources) ----

// Build a bare node of the given type without any GPU resource. Types the
// authoring-core knows how to reflect (LightNode / Water / ParticleSystem) are
// rebuilt as themselves; MeshNode is rebuilt with null mesh/material refs;
// everything else degrades to a generic Node (structure is preserved).
std::unique_ptr<Node> makeHeadlessNode(const std::string& type) {
    if (type == LightNode::reflectName()) return std::make_unique<LightNode>();
    if (type == WaterNode::reflectName()) return std::make_unique<WaterNode>();
    if (type == ParticleSystemNode::reflectName()) return std::make_unique<ParticleSystemNode>();
    if (type == "MeshNode") return std::make_unique<MeshNode>();
    return std::make_unique<Node>();
}

// Restore the resource-free fields written by serializeNodeWithoutResources.
// Children are handled by the caller so the root Scene can reuse this too.
void loadNodeFields(Node& node, const json& j) {
    if (auto it = j.find("id"); it != j.end() && it->is_number_integer())
        node.assignSerializedId(it->get<NodeId>());
    node.setName(j.value("name", node.name()));
    node.setEnabled(j.value("enabled", true));

    if (auto it = j.find("transform"); it != j.end() && it->is_object()) {
        Transform& t = node.transform();
        t.position = jsonToVec3(it->value("position", json()), t.position);
        if (auto r = it->find("rotation"); r != it->end() && r->is_array() && r->size() == 4)
            t.rotation = glm::quat((*r)[3].get<float>(), (*r)[0].get<float>(),
                                   (*r)[1].get<float>(), (*r)[2].get<float>());
        t.scale = jsonToVec3(it->value("scale", json()), t.scale);
    }

    if (auto it = j.find("groups"); it != j.end() && it->is_array())
        for (const auto& g : *it)
            if (g.is_string()) node.addToGroup(g.get<std::string>());

    if (auto it = j.find("importedFrom"); it != j.end() && it->is_string())
        node.setImportedFromPath(it->get<std::string>());

    if (const reflect::TypeDesc* desc = reflectedNodeDesc(node))
        desc->loadFrom(&node, j);

    // Behaviours: rebuild by type from the registry, then load reflected props.
    if (auto it = j.find("behaviours"); it != j.end() && it->is_array()) {
        registerReflectedTypes();  // idempotent: ensure the factory registry is ready
        for (const auto& bj : *it) {
            if (!bj.contains("type") || !bj["type"].is_string()) continue;
            const std::string tn = bj["type"].get<std::string>();
            if (tn == "SceneSettings") continue;  // legacy; handled by scene settings
            if (auto b = BehaviourRegistry::instance().create(tn)) {
                if (bj.contains("enabled") && bj["enabled"].is_boolean())
                    b->setEnabled(bj["enabled"].get<bool>());
                b->load(bj);
                node.addBehaviour(std::move(b));
            }
        }
    }
}

std::unique_ptr<Node> deserializeNodeHeadless(const json& j) {
    std::unique_ptr<Node> node = makeHeadlessNode(j.value("type", std::string("Node")));
    loadNodeFields(*node, j);
    if (auto it = j.find("children"); it != j.end() && it->is_array())
        for (const json& cj : *it)
            if (cj.is_object()) node->addChild(deserializeNodeHeadless(cj));
    return node;
}

// Mirror of sceneSettingsToJson — reads exactly the keys it writes, so a
// snapshot round-trips (serialize -> deserialize -> serialize) unchanged.
void loadSceneSettings(Scene& scene, const json& s) {
    SceneSettings& out = scene.settings();
    out.ambientLight = glm::vec4(jsonToVec3(s.value("ambient", json())), 0.0f);
    out.clearColor = glm::vec4(jsonToVec3(s.value("clearColor", json())), 1.0f);
    out.enablePostProcessing = s.value("postProcessing", true);
    out.lightingMode = static_cast<LightingMode>(s.value("lightingMode", 0));
    out.giEnabled = s.value("giEnabled", true);
    out.giMode = static_cast<GIMode>(s.value("giMode", 0));
    out.giIntensity = s.value("giIntensity", 1.0f);
    out.skyboxTexture = s.value("skyboxTexture", kAssetInvalid);
    out.skyboxExposure = s.value("skyboxExposure", 1.0f);
    out.skyboxRotation = s.value("skyboxRotation", 0.0f);
    out.iblEnabled = s.value("iblEnabled", true);
    out.iblDiffuseIntensity = s.value("iblDiffuseIntensity", 0.35f);
    out.iblSpecularIntensity = s.value("iblSpecularIntensity", 1.0f);
    out.aoEnabled = s.value("aoEnabled", true);
    out.aoRadius = s.value("aoRadius", 0.75f);
    out.aoIntensity = s.value("aoIntensity", 1.0f);
    out.aoPower = s.value("aoPower", 1.35f);
    out.fogEnabled = s.value("fogEnabled", false);
    if (s.contains("fogColor"))
        out.fogColor = glm::vec4(jsonToVec3(s.value("fogColor", json())), 1.0f);
    out.fogStart = s.value("fogStart", 8.0f);
    out.fogDensity = s.value("fogDensity", 0.035f);
    out.bloomEnabled = s.value("bloomEnabled", true);
    out.bloomThreshold = s.value("bloomThreshold", 1.0f);
    out.bloomIntensity = s.value("bloomIntensity", 0.25f);
    out.bloomRadius = s.value("bloomRadius", 3.0f);
    out.changeRenderingAtLoad = s.value("changeRenderingAtLoad", true);
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

bool deserializeSceneSnapshot(const json& doc, Scene& out, std::string* error) {
    auto fail = [&](const std::string& m) {
        if (error) *error = m;
        return false;
    };

    // Leave a clean, empty scene behind on any failure (never a half-built one).
    out.clearChildren();
    out.clearConnections();
    out.connections().clear();
    out.settings() = SceneSettings{};

    if (!doc.is_object()) return fail("snapshot document must be a JSON object");
    auto it = doc.find("scene");
    if (it == doc.end() || !it->is_object())
        return fail("snapshot document needs a 'scene' object");
    const json& root = *it;

    loadNodeFields(out, root);
    if (!root.contains("id")) out.regenerateId();

    if (auto sit = root.find("settings"); sit != root.end() && sit->is_object())
        loadSceneSettings(out, *sit);

    if (auto cit = root.find("children"); cit != root.end() && cit->is_array())
        for (const json& cj : *cit)
            if (cj.is_object()) out.addChild(deserializeNodeHeadless(cj));

    out.readConnections(root);
    Node::g_hierarchyVersion++;
    Node::g_transformVersion++;
    return true;
}

bool deserializeSceneSnapshot(const std::string& text, Scene& out, std::string* error) {
    json doc = json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded()) {
        if (error) *error = "invalid JSON";
        out.clearChildren();
        return false;
    }
    return deserializeSceneSnapshot(doc, out, error);
}

} // namespace saida::authoring
