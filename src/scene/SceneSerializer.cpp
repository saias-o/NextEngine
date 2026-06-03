#include "scene/SceneSerializer.hpp"

#include "core/Log.hpp"
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
    if (LightNode* light = node.asLight()) {
        j["type"] = "LightNode";
        j["lightType"] = static_cast<int>(light->type);
        j["color"] = vec3ToJson(light->color);
        j["intensity"] = light->intensity;
        j["direction"] = vec3ToJson(light->direction);
        j["range"] = light->range;
        j["bakeMode"] = static_cast<int>(light->bakeMode);
    } else if (node.mesh()) {
        j["type"] = "MeshNode";
        j["mesh"] = resources.meshKey(node.mesh());
        j["material"] = resources.materialKey(node.material());
    } else {
        j["type"] = "Node";
    }

    j["name"] = node.name();

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
    if (type == "LightNode") {
        auto light = std::make_unique<LightNode>(name,
            static_cast<LightType>(j.value("lightType", 0)));
        light->color = jsonToVec3(j.value("color", json()), glm::vec3(1.0f));
        light->intensity = j.value("intensity", 1.0f);
        light->direction = jsonToVec3(j.value("direction", json()), glm::vec3(0, -1, 0));
        light->range = j.value("range", 10.0f);
        light->bakeMode = static_cast<LightBakeMode>(j.value("bakeMode", 0));
        node = std::move(light);
    } else if (type == "MeshNode") {
        Mesh* mesh = resources.mesh(j.value("mesh", std::string{}));
        Material* material = resources.material(j.value("material", std::string{}));
        node = std::make_unique<MeshNode>(name, mesh, material);
    } else {
        node = std::make_unique<Node>(name);
    }

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
        if (auto it = root.find("children"); it != root.end() && it->is_array())
            for (const json& cj : *it)
                scene.addChild(deserializeNode(cj, resources));

        Log::info("loaded scene from ", path);
        return true;
    } catch (const std::exception& e) {
        Log::error("loadIntoScene: ", e.what());
        return false;
    }
}

} // namespace ne
