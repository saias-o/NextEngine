#include "scene/SceneSerializer.hpp"

#include "core/Log.hpp"
#include "graphics/Material.hpp"
#include "graphics/ResourceManager.hpp"
#include "scene/Behaviour.hpp"
#include "scene/BehaviourRegistry.hpp"
#include "scene/LightNode.hpp"
#include "scene/MeshNode.hpp"
#include "scene/Node.hpp"
#include "scene/Scene.hpp"

#include "nlohmann/json.hpp"

#include <fstream>

namespace ne {

namespace {
using json = nlohmann::json;

constexpr int kSceneVersion = 1;

json vec3ToJson(const glm::vec3& v) { return json::array({v.x, v.y, v.z}); }

glm::vec3 jsonToVec3(const json& j, const glm::vec3& fallback = glm::vec3(0.0f)) {
    if (!j.is_array() || j.size() != 3) return fallback;
    return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
}
glm::vec4 jsonToVec4(const json& j, const glm::vec4& fallback = glm::vec4(0.0f)) {
    if (!j.is_array() || j.size() != 4) return fallback;
    return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>()};
}

// ── Node -> JSON ─────────────────────────────────────────────────────────────
json serializeNode(Node& node, ResourceManager& resources) {
    json j;
    if (Scene* scene = dynamic_cast<Scene*>(&node)) {
        j["type"] = "Scene";
        const auto& s = scene->settings();
        j["settings"] = {
            {"ambient", vec3ToJson(s.ambientLight)},
            {"clearColor", vec3ToJson(s.clearColor)},
            {"postProcessing", s.enablePostProcessing},
            {"lightingMode", static_cast<int>(s.lightingMode)}
        };
    } else if (LightNode* light = node.asLight()) {
        j["type"] = "LightNode";
        j["lightType"] = static_cast<int>(light->type);
        j["color"] = vec3ToJson(light->color);
        j["intensity"] = light->intensity;
        j["direction"] = vec3ToJson(light->direction);
        j["range"] = light->range;
        j["spotInnerAngle"] = light->spotInnerAngle;
        j["spotOuterAngle"] = light->spotOuterAngle;
        j["castShadows"] = light->castShadows;
        j["bakeMode"] = static_cast<int>(light->bakeMode);
    } else if (MeshNode* meshNode = dynamic_cast<MeshNode*>(&node)) {
        j["type"] = "MeshNode";
        j["mesh"] = resources.meshId(node.mesh());
        if (Material* mat = node.material()) {
            j["texture"] = mat->desc().albedoId;
            j["baseColor"] = {mat->desc().baseColor.r, mat->desc().baseColor.g,
                              mat->desc().baseColor.b, mat->desc().baseColor.a};
        }
        j["castShadows"] = meshNode->castShadows();
        j["includeInLightBaking"] = meshNode->includeInLightBaking();
    } else {
        j["type"] = "Node";
    }

    j["name"] = node.name();
    j["enabled"] = node.enabled();

    const Transform& t = node.transform();
    j["transform"] = {
        {"position", vec3ToJson(t.position)},
        {"rotation", json::array({t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w})},
        {"scale", vec3ToJson(t.scale)},
    };

    json behaviours = json::array();
    for (const auto& b : node.behaviours()) {
        if (const char* tn = b->typeName()) {
            json bj;
            bj["type"] = tn;
            b->save(bj);
            behaviours.push_back(std::move(bj));
        }
    }
    j["behaviours"] = std::move(behaviours);

    json children = json::array();
    for (const auto& child : node.children())
        children.push_back(serializeNode(*child, resources));
    j["children"] = std::move(children);

    return j;
}

// ── JSON -> Node ─────────────────────────────────────────────────────────────
std::unique_ptr<Node> deserializeNode(const json& j, ResourceManager& resources) {
    const std::string type = j.value("type", "Node");
    const std::string name = j.value("name", "Node");

    std::unique_ptr<Node> node;
    if (type == "Scene") {
        auto scene = std::make_unique<Scene>();
        if (auto it = j.find("settings"); it != j.end()) {
            scene->settings().ambientLight = glm::vec4(jsonToVec3(it->value("ambient", json())), 0.0f);
            scene->settings().clearColor = glm::vec4(jsonToVec3(it->value("clearColor", json())), 1.0f);
            scene->settings().enablePostProcessing = it->value("postProcessing", true);
            scene->settings().lightingMode = static_cast<LightingMode>(it->value("lightingMode", 0));
        }
        node = std::move(scene);
    } else if (type == "LightNode") {
        auto light = std::make_unique<LightNode>(name,
            static_cast<LightType>(j.value("lightType", 0)));
        light->color = jsonToVec3(j.value("color", json()), glm::vec3(1.0f));
        light->intensity = j.value("intensity", 1.0f);
        light->direction = jsonToVec3(j.value("direction", json()), glm::vec3(0, -1, 0));
        light->range = j.value("range", 10.0f);
        light->spotInnerAngle = j.value("spotInnerAngle", 25.0f);
        light->spotOuterAngle = j.value("spotOuterAngle", 35.0f);
        light->castShadows = j.value("castShadows", true);
        light->bakeMode = static_cast<LightBakeMode>(j.value("bakeMode", 0));
        node = std::move(light);
    } else if (type == "MeshNode") {
        AssetID meshId = kAssetInvalid;
        if (j.contains("mesh")) {
            if (j["mesh"].is_number_integer()) {
                meshId = j["mesh"].get<AssetID>();
            } else if (j["mesh"].is_string()) {
                meshId = resources.getOrRegister(j["mesh"].get<std::string>(), AssetType::Mesh);
            }
        }
        
        Mesh* mesh = resources.getMesh(meshId);
        Material* material = nullptr;
        if (auto it = j.find("texture"); it != j.end()) {
            AssetID texID = kAssetInvalid;
            if (it->is_number_integer()) {
                texID = it->get<AssetID>();
            } else if (it->is_string()) {
                texID = resources.getOrRegister(it->get<std::string>(), AssetType::Texture);
            }

            glm::vec4 color = jsonToVec4(j.value("baseColor", json()), glm::vec4(1.0f));
            MaterialDesc desc;
            desc.albedoId = texID;
            desc.baseColor = color;
            material = resources.getMaterial(desc);
        }
        auto meshNode = std::make_unique<MeshNode>(name, mesh, material);
        meshNode->castShadows() = j.value("castShadows", true);
        meshNode->includeInLightBaking() = j.value("includeInLightBaking", false);
        node = std::move(meshNode);
    } else {
        node = std::make_unique<Node>(name);
    }

    node->setEnabled(j.value("enabled", true));

    if (auto it = j.find("transform"); it != j.end()) {
        const json& tj = *it;
        Transform& t = node->transform();
        t.position = jsonToVec3(tj.value("position", json()));
        glm::vec4 r = jsonToVec4(tj.value("rotation", json()), glm::vec4(0, 0, 0, 1));
        t.rotation = glm::quat(r.w, r.x, r.y, r.z);  // glm::quat(w, x, y, z)
        t.scale = jsonToVec3(tj.value("scale", json()), glm::vec3(1.0f));
    }

    if (auto it = j.find("behaviours"); it != j.end() && it->is_array()) {
        for (const json& bj : *it) {
            const std::string bt = bj.value("type", std::string{});
            if (auto behaviour = BehaviourRegistry::instance().create(bt)) {
                behaviour->load(bj);
                node->addBehaviour(std::move(behaviour));
            } else if (!bt.empty()) {
                Log::warn("scene load: unknown behaviour type '", bt, "' (skipped)");
            }
        }
    }

    if (auto it = j.find("children"); it != j.end() && it->is_array())
        for (const json& cj : *it)
            node->addChild(deserializeNode(cj, resources));

    return node;
}

} // namespace

std::string SceneSerializer::nodeToJson(Node& node, ResourceManager& resources) {
    return serializeNode(node, resources).dump(2);
}

std::unique_ptr<Node> SceneSerializer::nodeFromJson(const std::string& text,
                                                    ResourceManager& resources) {
    try {
        return deserializeNode(json::parse(text), resources);
    } catch (const std::exception& e) {
        Log::error("nodeFromJson: ", e.what());
        return nullptr;
    }
}

std::unique_ptr<Node> SceneSerializer::loadNodeFromSceneFile(const std::string& path,
                                                             ResourceManager& resources) {
    std::ifstream file(path);
    if (!file.is_open()) {
        Log::error("loadNodeFromSceneFile: cannot open ", path);
        return nullptr;
    }
    try {
        json doc = json::parse(file);
        return deserializeNode(doc.at("scene"), resources);
    } catch (const std::exception& e) {
        Log::error("loadNodeFromSceneFile: ", e.what());
        return nullptr;
    }
}

bool SceneSerializer::saveToFile(Node& sceneRoot, ResourceManager& resources,
                                 const std::string& path) {
    json doc;
    doc["version"] = kSceneVersion;
    doc["scene"] = serializeNode(sceneRoot, resources);

    std::ofstream file(path);
    if (!file.is_open()) {
        Log::error("saveToFile: cannot write ", path);
        return false;
    }
    file << doc.dump(2) << "\n";
    Log::info("saved scene to ", path);
    return true;
}

bool SceneSerializer::loadIntoScene(Scene& scene, ResourceManager& resources,
                                    const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        Log::error("loadIntoScene: cannot open ", path);
        return false;
    }

    try {
        json doc = json::parse(file);
        const json& root = doc.at("scene");

        scene.clearChildren();
        
        // Load native scene settings (if new format)
        if (auto it = root.find("settings"); it != root.end()) {
            scene.settings().ambientLight = glm::vec4(jsonToVec3(it->value("ambient", json())), 0.0f);
            scene.settings().clearColor = glm::vec4(jsonToVec3(it->value("clearColor", json())), 1.0f);
            scene.settings().enablePostProcessing = it->value("postProcessing", true);
        } else {
            scene.settings() = SceneSettings{}; // Reset to defaults if missing
        }

        if (auto it = root.find("children"); it != root.end() && it->is_array()) {
            for (const json& cj : *it) {
                // Backwards compatibility for old SceneSettingsBehaviour
                if (cj.value("name", "") == "Settings") {
                    if (auto bit = cj.find("behaviours"); bit != cj.end() && bit->is_array()) {
                        for (const json& bj : *bit) {
                            if (bj.value("type", "") == "SceneSettings") {
                                scene.settings().ambientLight = glm::vec4(jsonToVec3(bj.value("ambient", json())), 0.0f);
                                scene.settings().clearColor = glm::vec4(jsonToVec3(bj.value("clearColor", json())), 1.0f);
                                scene.settings().enablePostProcessing = bj.value("postProcessing", true);
                            }
                        }
                    }
                    continue; // Skip creating the legacy node
                }
                scene.addChild(deserializeNode(cj, resources));
            }
        }

        Log::info("loaded scene from ", path);
        return true;
    } catch (const std::exception& e) {
        Log::error("loadIntoScene: ", e.what());
        return false;
    }
}

} // namespace ne
