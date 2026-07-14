#include "scene/SceneSerializer.hpp"

#include "core/FormatVersions.hpp"
#include "core/Log.hpp"
#include "core/Paths.hpp"
#include "graphics/Material.hpp"
#include "graphics/ResourceManager.hpp"
#include "scene/Behaviour.hpp"
#include "scene/BehaviourRegistry.hpp"
#include "scene/GLTFLoader.hpp"
#include "scene/LightNode.hpp"
#include "scene/MeshNode.hpp"
#include "scene/Node.hpp"
#include "scene/NodeRegistry.hpp"
#include "scene/Scene.hpp"
#include "scene/SerializationHelpers.hpp"

#include "nlohmann/json.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <unordered_set>

namespace saida {

namespace {
using json = nlohmann::json;

bool isModelPath(const std::string& path) {
    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext == ".gltf" || ext == ".glb";
}

bool reloadImportedModel(Node& target, const std::string& path, ResourceManager& resources) {
    if (!isModelPath(path)) return false;

    // Chemin relatif = relatif à la racine du projet chargé (comme les scripts).
    std::string resolved = path;
    if (std::filesystem::path p(path); p.is_relative() && !activeProjectRoot().empty()) {
        std::filesystem::path abs = std::filesystem::path(activeProjectRoot()) / p;
        if (std::filesystem::exists(abs)) resolved = abs.string();
    }

    Node temp("ImportRoot");
    GLTFLoadOptions opts;
    if (!GLTFLoader::load(resolved, temp, resources, opts))
        return false;

    if (temp.children().empty())
        return false;

    Node* loadedContainer = temp.children().front().get();
    while (!loadedContainer->children().empty()) {
        target.addChild(loadedContainer->detachChild(loadedContainer->children().front().get()));
    }
    return true;
}

json serializeNode(Node& node, ResourceManager& resources) {
    json j;
    node.serialize(j, resources);
    return j;
}

std::unique_ptr<Node> deserializeNode(const json& j, ResourceManager& resources,
                                      NodeIdPolicy idPolicy) {
    const std::string type = j.value("type", "Node");

    // Special case for scene prefabs
    if (type == "Scene" && j.contains("prefabAssetId")) {
        AssetID prefabId = j["prefabAssetId"].get<AssetID>();
        if (prefabId != kAssetInvalid && resources.getRegistry()) {
            std::string absPath = resources.getRegistry()->getAbsolutePath(prefabId);
            if (!absPath.empty()) {
                // Load the nested scene from file instead of reading children
                std::unique_ptr<Node> loaded = SceneSerializer::loadNodeFromSceneFile(
                    absPath, resources, NodeIdPolicy::Regenerate);
                if (loaded) {
                    loaded->deserialize(j, resources); // apply settings overrides on top of the prefab
                    if (idPolicy == NodeIdPolicy::Preserve && j.contains("id"))
                        loaded->assignSerializedId(j["id"].get<NodeId>());
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
    if (idPolicy == NodeIdPolicy::Preserve && j.contains("id"))
        node->assignSerializedId(j["id"].get<NodeId>());
    else if (idPolicy == NodeIdPolicy::Regenerate)
        node->regenerateId();
    bool childrenLoadedFromImport = false;
    if (!node->importedFromPath().empty()) {
        childrenLoadedFromImport = reloadImportedModel(*node, node->importedFromPath(), resources);
        if (!childrenLoadedFromImport) {
            Log::warn("SceneSerializer: failed to reload imported model ",
                      node->importedFromPath(), ", falling back to serialized children.");
        }
    }
    
    // We handle children deserialization here because the parent node
    // shouldn't manually instantiate children, the SceneSerializer does that using NodeRegistry.
    if (!childrenLoadedFromImport) {
        if (auto it = j.find("children"); it != j.end() && it->is_array()) {
            for (const json& cj : *it) {
                if (auto child = deserializeNode(cj, resources, idPolicy)) {
                    node->addChild(std::move(child));
                }
            }
        }
    }

    // If it's a prefab instance, load its children from the original file
    if (Scene* s = dynamic_cast<Scene*>(node.get())) {
        if (s->prefabAssetId() != kAssetInvalid && resources.getRegistry()) {
            std::string path = resources.getRegistry()->getAbsolutePath(s->prefabAssetId());
            if (!path.empty()) {
                if (auto prefabNode = SceneSerializer::loadNodeFromSceneFile(
                        path, resources, NodeIdPolicy::Regenerate)) {
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

void ensureUniqueIds(Node& root) {
    std::unordered_set<NodeId> seen;
    root.traverse([&](Node& node, const glm::mat4&) {
        while (node.id() == kNodeInvalid || !seen.insert(node.id()).second)
            node.regenerateId();
        seen.insert(node.id());
    });
}

bool acceptSceneDocumentVersion(const json& doc, const std::string& context,
                                const std::string& path) {
    if (!doc.is_object()) {
        Log::error(context, ": scene document root must be an object: ", path);
        return false;
    }
    if (doc.contains("schema") && !doc["schema"].is_number_integer()) {
        Log::error(context, ": scene document schema must be an integer: ", path);
        return false;
    }

    const int version = format::readSchema(doc, format::kLegacyVersion);
    if (version > format::kSceneVersion) {
        Log::error(context, ": unsupported scene format v", version,
                   " (supported v", format::kSceneVersion, "): ", path);
        return false;
    }
    if (!format::hasIntegerSchema(doc)) {
        Log::info(context, ": migrated legacy scene schema v", version, " -> v",
                  format::kSceneVersion, " in memory: ", path);
    } else if (version < format::kSceneVersion) {
        Log::info(context, ": migrated scene format v", version, " -> v",
                  format::kSceneVersion, " in memory: ", path);
    }
    return true;
}

} // namespace

std::string SceneSerializer::nodeToJson(Node& node, ResourceManager& resources) {
    return serializeNode(node, resources).dump(2);
}

std::unique_ptr<Node> SceneSerializer::nodeFromJson(const std::string& text,
                                                    ResourceManager& resources,
                                                    NodeIdPolicy idPolicy) {
    try {
        auto node = deserializeNode(json::parse(text), resources, idPolicy);
        if (node) ensureUniqueIds(*node);
        return node;
    } catch (const std::exception& e) {
        Log::error("nodeFromJson: ", e.what());
        return nullptr;
    }
}

std::unique_ptr<Node> SceneSerializer::loadNodeFromSceneFile(const std::string& path,
                                                             ResourceManager& resources,
                                                             NodeIdPolicy idPolicy) {
    std::ifstream file(path);
    if (!file.is_open()) {
        Log::error("loadNodeFromSceneFile: cannot open ", path);
        return nullptr;
    }
    try {
        json doc = json::parse(file);
        if (!acceptSceneDocumentVersion(doc, "loadNodeFromSceneFile", path))
            return nullptr;
        json root = doc.at("scene");
        root["type"] = "Scene"; // Force the root node to be instantiated as a Scene
        auto node = deserializeNode(root, resources, idPolicy);
        if (node) ensureUniqueIds(*node);
        return node;
    } catch (const std::exception& e) {
        Log::error("loadNodeFromSceneFile: ", e.what());
        return nullptr;
    }
}

bool SceneSerializer::validateSceneDocumentFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        Log::error("validateSceneDocumentFile: cannot open ", path);
        return false;
    }
    try {
        json doc = json::parse(file);
        if (!acceptSceneDocumentVersion(doc, "validateSceneDocumentFile", path))
            return false;
        if (!doc.contains("scene") || !doc["scene"].is_object()) {
            Log::error("validateSceneDocumentFile: missing 'scene' object: ", path);
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        Log::error("validateSceneDocumentFile: ", e.what());
        return false;
    }
}

bool SceneSerializer::saveToFile(Node& sceneRoot, ResourceManager& resources,
                                 const std::string& path) {
    json doc;
    format::writeSchema(doc, format::kSceneVersion);
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
        if (!acceptSceneDocumentVersion(doc, "loadIntoScene", path))
            return false;
        const json& root = doc.at("scene");

        scene.clearChildren();
        if (root.contains("id")) scene.assignSerializedId(root["id"].get<NodeId>());
        else scene.regenerateId();
        
        // Load native scene settings (if new format)
        if (auto it = root.find("settings"); it != root.end()) {
            scene.settings().ambientLight = glm::vec4(jsonToVec3(it->value("ambient", json())), 0.0f);
            scene.settings().clearColor = glm::vec4(jsonToVec3(it->value("clearColor", json())), 1.0f);
            scene.settings().enablePostProcessing = it->value("postProcessing", true);
            scene.settings().lightingMode = static_cast<LightingMode>(it->value("lightingMode", 0));
            scene.settings().giEnabled = it->value("giEnabled", true);
            scene.settings().giMode = static_cast<GIMode>(it->value("giMode", 0));
            scene.settings().giIntensity = it->value("giIntensity", 1.0f);
            scene.settings().skyboxTexture = it->value("skyboxTexture", kAssetInvalid);
            scene.settings().skyboxExposure = it->value("skyboxExposure", 1.0f);
            scene.settings().skyboxRotation = it->value("skyboxRotation", 0.0f);
            scene.settings().iblEnabled = it->value("iblEnabled", true);
            scene.settings().iblDiffuseIntensity = it->value("iblDiffuseIntensity", 0.35f);
            scene.settings().iblSpecularIntensity = it->value("iblSpecularIntensity", 1.0f);
            scene.settings().aoEnabled = it->value("aoEnabled", true);
            scene.settings().aoRadius = it->value("aoRadius", 0.75f);
            scene.settings().aoIntensity = it->value("aoIntensity", 1.0f);
            scene.settings().aoPower = it->value("aoPower", 1.35f);
            scene.settings().fogEnabled = it->value("fogEnabled", false);
            if (it->contains("fogColor"))
                scene.settings().fogColor = glm::vec4(jsonToVec3(it->value("fogColor", json())), 1.0f);
            scene.settings().fogStart = it->value("fogStart", 8.0f);
            scene.settings().fogDensity = it->value("fogDensity", 0.035f);
            scene.settings().bloomEnabled = it->value("bloomEnabled", true);
            scene.settings().bloomThreshold = it->value("bloomThreshold", 1.0f);
            scene.settings().bloomIntensity = it->value("bloomIntensity", 0.25f);
            scene.settings().bloomRadius = it->value("bloomRadius", 3.0f);
            scene.settings().changeRenderingAtLoad = it->value("changeRenderingAtLoad", true);
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
                                scene.settings().iblEnabled = bj.value("iblEnabled", true);
                                scene.settings().iblDiffuseIntensity = bj.value("iblDiffuseIntensity", 0.35f);
                                scene.settings().iblSpecularIntensity = bj.value("iblSpecularIntensity", 1.0f);
                                scene.settings().aoEnabled = bj.value("aoEnabled", true);
                                scene.settings().aoRadius = bj.value("aoRadius", 0.75f);
                                scene.settings().aoIntensity = bj.value("aoIntensity", 1.0f);
                                scene.settings().aoPower = bj.value("aoPower", 1.35f);
                                scene.settings().fogEnabled = bj.value("fogEnabled", false);
                                if (bj.contains("fogColor"))
                                    scene.settings().fogColor = glm::vec4(jsonToVec3(bj.value("fogColor", json())), 1.0f);
                                scene.settings().fogStart = bj.value("fogStart", 8.0f);
                                scene.settings().fogDensity = bj.value("fogDensity", 0.035f);
                                scene.settings().bloomEnabled = bj.value("bloomEnabled", true);
                                scene.settings().bloomThreshold = bj.value("bloomThreshold", 1.0f);
                                scene.settings().bloomIntensity = bj.value("bloomIntensity", 0.25f);
                                scene.settings().bloomRadius = bj.value("bloomRadius", 3.0f);
                            }
                        }
                    }
                    continue; // Skip creating the legacy node
                }
                scene.addChild(deserializeNode(cj, resources, NodeIdPolicy::Preserve));
            }
        }

        // Scene-level data-driven signal connections (loadIntoScene parses the
        // root manually, so read them here in addition to Scene::deserialize).
        scene.readConnections(root);

        ensureUniqueIds(scene);

        Log::info("loaded scene from ", path);
        return true;
    } catch (const std::exception& e) {
        Log::error("loadIntoScene: ", e.what());
        return false;
    }
}

} // namespace saida
