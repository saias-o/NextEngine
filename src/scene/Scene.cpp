#include "scene/Scene.hpp"
#include "scene/Behaviour.hpp"
#include "scene/MeshNode.hpp"
#include "scene/LightNode.hpp"
#include "scene/SerializationHelpers.hpp"
#include "graphics/ResourceManager.hpp"

#include <nlohmann/json.hpp>

namespace ne {

Scene::Scene() : Node("Scene") {
}

void Scene::update(float dt) {
    if (lastHierarchyVersion_ != g_hierarchyVersion) {
        flattenHierarchy();
        lastHierarchyVersion_ = g_hierarchyVersion;
    }

    for (auto* b : flatBehaviours_) {
        if (!b->enabled()) continue;
        if (dt > 0.0f) {
            if (!b->ready_) {
                b->onReady();
                b->ready_ = true;
            }
            b->onUpdate(dt);
        }
    }

    updateTransforms(glm::mat4(1.0f), false);
}

void Scene::flattenHierarchy() {
    meshes_.clear();
    lights_.clear();
    flatBehaviours_.clear();

    traverse([this](Node& n, const glm::mat4&) {
        if (!n.isActiveInHierarchy()) return;
        
        if (MeshNode* mn = dynamic_cast<MeshNode*>(&n)) {
            if (mn->meshEnabled()) {
                meshes_.push_back(mn);
            }
        }
        if (n.asLight()) {
            lights_.push_back(static_cast<LightNode*>(&n));
        }
        for (auto& b : n.behaviours()) {
            flatBehaviours_.push_back(b.get());
        }
    });
}

void Scene::serialize(nlohmann::json& j, ResourceManager& resources) const {
    Node::serialize(j, resources);
    if (prefabAssetId_ != kAssetInvalid) {
        j["prefabAssetId"] = prefabAssetId_;
        j.erase("children"); // Do not serialize children for prefabs
    }
    j["settings"] = {
        {"ambient", vec3ToJson(settings_.ambientLight)},
        {"clearColor", vec3ToJson(settings_.clearColor)},
        {"postProcessing", settings_.enablePostProcessing},
        {"lightingMode", static_cast<int>(settings_.lightingMode)},
        {"skyboxTexture", settings_.skyboxTexture},
        {"skyboxExposure", settings_.skyboxExposure},
        {"skyboxRotation", settings_.skyboxRotation}
    };
}

void Scene::deserialize(const nlohmann::json& j, ResourceManager& resources) {
    Node::deserialize(j, resources);
    if (j.contains("prefabAssetId")) {
        prefabAssetId_ = j["prefabAssetId"].get<AssetID>();
    }
    if (j.contains("settings")) {
        auto js = j["settings"];
        settings_.ambientLight = glm::vec4(jsonToVec3(js["ambient"], glm::vec3(0.1f)), 1.0f);
        settings_.clearColor = glm::vec4(jsonToVec3(js["clearColor"], glm::vec3(0.0f)), 1.0f);
        if (js.contains("postProcessing")) settings_.enablePostProcessing = js["postProcessing"].get<bool>();
        if (js.contains("lightingMode")) settings_.lightingMode = static_cast<LightingMode>(js["lightingMode"].get<int>());
        if (js.contains("skyboxTexture")) {
            if (js["skyboxTexture"].is_number_integer()) {
                settings_.skyboxTexture = js["skyboxTexture"].get<AssetID>();
            } else if (js["skyboxTexture"].is_string()) {
                settings_.skyboxTexture = resources.getOrRegister(js["skyboxTexture"].get<std::string>(), AssetType::Texture);
            }
        }
        if (js.contains("skyboxExposure")) settings_.skyboxExposure = js["skyboxExposure"].get<float>();
        if (js.contains("skyboxRotation")) settings_.skyboxRotation = js["skyboxRotation"].get<float>();
    }

    // Backwards compatibility for old SceneSettingsBehaviour
    if (j.contains("behaviours") && j["behaviours"].is_array()) {
        for (const auto& bj : j["behaviours"]) {
            if (bj.contains("type") && bj["type"].get<std::string>() == "SceneSettings") {
                if (bj.contains("ambient")) settings_.ambientLight = glm::vec4(jsonToVec3(bj["ambient"], glm::vec3(0.1f)), 1.0f);
                if (bj.contains("clearColor")) settings_.clearColor = glm::vec4(jsonToVec3(bj["clearColor"], glm::vec3(0.0f)), 1.0f);
                if (bj.contains("postProcessing")) settings_.enablePostProcessing = bj["postProcessing"].get<bool>();
                if (bj.contains("lightingMode")) settings_.lightingMode = static_cast<LightingMode>(bj["lightingMode"].get<int>());
                if (bj.contains("skyboxTexture")) {
                    if (bj["skyboxTexture"].is_number_integer()) {
                        settings_.skyboxTexture = bj["skyboxTexture"].get<AssetID>();
                    } else if (bj["skyboxTexture"].is_string()) {
                        settings_.skyboxTexture = resources.getOrRegister(bj["skyboxTexture"].get<std::string>(), AssetType::Texture);
                    }
                }
                if (bj.contains("skyboxExposure")) settings_.skyboxExposure = bj["skyboxExposure"].get<float>();
                if (bj.contains("skyboxRotation")) settings_.skyboxRotation = bj["skyboxRotation"].get<float>();
            }
        }
    }
}

} // namespace ne
