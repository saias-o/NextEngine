#include "scene/SceneSerializer.hpp"

#include "core/Log.hpp"
#include "graphics/Material.hpp"
#include "graphics/ResourceManager.hpp"
#include "scene/Behaviour.hpp"
#include "scene/BehaviourRegistry.hpp"
#include "scene/LightNode.hpp"
#include "scene/MeshNode.hpp"
#include "scene/Node.hpp"
#include "scene/NodeRegistry.hpp"
#include "scene/Scene.hpp"
#include "scene/SerializationHelpers.hpp"

#include "nlohmann/json.hpp"

#include <fstream>

namespace ne {

namespace {
using json = nlohmann::json;

constexpr int kSceneVersion = 1;

// ── Node -> JSON ─────────────────────────────────────────────────────────────
json serializeNode(Node& node, ResourceManager& resources) {
    json j;
    node.serialize(j, resources);
    return j;
}

// ── JSON -> Node ─────────────────────────────────────────────────────────────
std::unique_ptr<Node> deserializeNode(const json& j, ResourceManager& resources) {
    const std::string type = j.value("type", "Node");

    // Special case for scene prefabs
    if (type == "Scene" && j.contains("prefabAssetId")) {
        AssetID prefabId = j["prefabAssetId"].get<AssetID>();
        if (prefabId != kAssetInvalid && resources.getRegistry()) {
            std::string absPath = resources.getRegistry()->getAbsolutePath(prefabId);
            if (!absPath.empty()) {
                // Load the nested scene from file instead of reading children
                std::unique_ptr<Node> loaded = SceneSerializer::loadNodeFromSceneFile(absPath, resources);
                if (loaded) {
                    loaded->deserialize(j, resources); // apply settings overrides on top of the prefab
                    return loaded;
                }
            }
        }
    }

    std::unique_ptr<Node> node = NodeRegistry::instance().create(type);
    if (!node) {
        Log::warn("SceneSerializer: Unknown node type '", type, "', falling back to generic Node.");
        node = std::make_unique<Node>();
    }

    node->deserialize(j, resources);
    
    // We handle children deserialization here because the parent node
    // shouldn't manually instantiate children, the SceneSerializer does that using NodeRegistry.
    if (auto it = j.find("children"); it != j.end() && it->is_array()) {
        for (const json& cj : *it) {
            if (auto child = deserializeNode(cj, resources)) {
                node->addChild(std::move(child));
            }
        }
    }

    // If it's a prefab instance, load its children from the original file
    if (Scene* s = dynamic_cast<Scene*>(node.get())) {
        if (s->prefabAssetId() != kAssetInvalid && resources.getRegistry()) {
            std::string path = resources.getRegistry()->getAbsolutePath(s->prefabAssetId());
            if (!path.empty()) {
                if (auto prefabNode = SceneSerializer::loadNodeFromSceneFile(path, resources)) {
                    // Move all children from the prefab file into this instance
                    // We only want the children, not the root settings of the prefab
                    while (!prefabNode->children().empty()) {
                        node->addChild(prefabNode->detachChild(prefabNode->children().front().get()));
                    }
                }
            }
        }
    }

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
        json root = doc.at("scene");
        root["type"] = "Scene"; // Force the root node to be instantiated as a Scene
        return deserializeNode(root, resources);
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
