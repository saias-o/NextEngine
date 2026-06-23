#include "scene/Scene.hpp"
#include "scene/Behaviour.hpp"
#include "scene/MeshNode.hpp"
#include "scene/LightNode.hpp"
#include "scene/SerializationHelpers.hpp"
#include "graphics/ResourceManager.hpp"
#include "physics/PhysicsWorld.hpp"
#include "physics/CollisionObjectNode.hpp"
#include "physics/AreaNode.hpp"

#include <nlohmann/json.hpp>

namespace ne {

Scene::Scene() : Node("Scene") {
}

Scene::~Scene() {
    // Destroy the node tree (and thus every physics body) while the PhysicsWorld
    // is still alive — the Node base subobject (which owns children_) is otherwise
    // torn down after this Scene's members, leaving body destructors with a
    // dangling world pointer.
    clearChildren();
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

    // Freeze each Auto collision shape once, now that world transforms are fresh
    // (runs in edit mode too, so the editor wireframe is stable and correct).
    for (auto* body : bodies_) body->resolveAutoShapes();

    // Physics only runs while time is advancing (i.e. in Play, not while editing).
    if (dt > 0.0f && !bodies_.empty()) {
        if (!physics_) physics_ = std::make_unique<PhysicsWorld>();
        for (auto* body : bodies_) body->syncToPhysics(*physics_);
        for (auto* body : bodies_) body->prePhysicsStep(*physics_, dt);  // characters move/slide
        physics_->step(dt);
        for (auto* body : bodies_) body->syncFromPhysics(*physics_);

        // Dispatch contact events on the main thread: sensor overlaps to Area
        // nodes, solid collisions to both bodies' collision signals.
        for (const auto& e : physics_->drainContactEvents()) {
            auto* n1 = static_cast<CollisionObjectNode*>(physics_->bodyUserData(e.a));
            auto* n2 = static_cast<CollisionObjectNode*>(physics_->bodyUserData(e.b));
            if (e.sensor) {
                if (auto* area = dynamic_cast<AreaNode*>(n1)) area->handleOverlap(n2, e.entered);
                if (auto* area = dynamic_cast<AreaNode*>(n2)) area->handleOverlap(n1, e.entered);
            } else {
                if (n1) (e.entered ? n1->collisionEntered : n1->collisionExited).emit(n2);
                if (n2) (e.entered ? n2->collisionEntered : n2->collisionExited).emit(n1);
            }
        }

        updateTransforms(glm::mat4(1.0f), false);  // propagate dynamic results down the tree
    }
}

void Scene::flattenHierarchy() {
    meshes_.clear();
    lights_.clear();
    flatBehaviours_.clear();
    bodies_.clear();

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
        if (CollisionObjectNode* co = n.asCollisionObject()) {
            bodies_.push_back(co);
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
        {"giEnabled", settings_.giEnabled},
        {"giIntensity", settings_.giIntensity},
        {"skyboxTexture", settings_.skyboxTexture},
        {"skyboxExposure", settings_.skyboxExposure},
        {"skyboxRotation", settings_.skyboxRotation},
        {"iblEnabled", settings_.iblEnabled},
        {"iblDiffuseIntensity", settings_.iblDiffuseIntensity},
        {"iblSpecularIntensity", settings_.iblSpecularIntensity},
        {"aoEnabled", settings_.aoEnabled},
        {"aoRadius", settings_.aoRadius},
        {"aoIntensity", settings_.aoIntensity},
        {"aoPower", settings_.aoPower},
        {"fogEnabled", settings_.fogEnabled},
        {"fogColor", vec3ToJson(glm::vec3(settings_.fogColor))},
        {"fogStart", settings_.fogStart},
        {"fogDensity", settings_.fogDensity},
        {"bloomEnabled", settings_.bloomEnabled},
        {"bloomThreshold", settings_.bloomThreshold},
        {"bloomIntensity", settings_.bloomIntensity},
        {"bloomRadius", settings_.bloomRadius},
        {"changeRenderingAtLoad", settings_.changeRenderingAtLoad}
    };

    if (!connectionDefs_.empty()) {
        nlohmann::json conns = nlohmann::json::array();
        for (const auto& c : connectionDefs_)
            conns.push_back({{"from", c.from}, {"signal", c.signal},
                             {"to", c.to}, {"slot", c.slot}});
        j["connections"] = std::move(conns);
    }
}

void Scene::readConnections(const nlohmann::json& j) {
    connectionDefs_.clear();
    auto it = j.find("connections");
    if (it == j.end() || !it->is_array()) return;
    for (const auto& cj : *it) {
        SignalConnectionDef def;
        def.from = cj.value("from", kNodeInvalid);
        def.signal = cj.value("signal", std::string{});
        def.to = cj.value("to", kNodeInvalid);
        def.slot = cj.value("slot", std::string{});
        if (def.from != kNodeInvalid && def.to != kNodeInvalid &&
            !def.signal.empty() && !def.slot.empty())
            connectionDefs_.push_back(std::move(def));
    }
}

void Scene::deserialize(const nlohmann::json& j, ResourceManager& resources) {
    Node::deserialize(j, resources);
    readConnections(j);
    if (j.contains("prefabAssetId")) {
        prefabAssetId_ = j["prefabAssetId"].get<AssetID>();
    }
    if (j.contains("settings")) {
        auto js = j["settings"];
        settings_.ambientLight = glm::vec4(jsonToVec3(js["ambient"], glm::vec3(0.1f)), 1.0f);
        settings_.clearColor = glm::vec4(jsonToVec3(js["clearColor"], glm::vec3(0.0f)), 1.0f);
        if (js.contains("postProcessing")) settings_.enablePostProcessing = js["postProcessing"].get<bool>();
        if (js.contains("lightingMode")) settings_.lightingMode = static_cast<LightingMode>(js["lightingMode"].get<int>());
        if (js.contains("giEnabled")) settings_.giEnabled = js["giEnabled"].get<bool>();
        if (js.contains("giIntensity")) settings_.giIntensity = js["giIntensity"].get<float>();
        if (js.contains("skyboxTexture")) {
            if (js["skyboxTexture"].is_number_integer()) {
                settings_.skyboxTexture = js["skyboxTexture"].get<AssetID>();
            } else if (js["skyboxTexture"].is_string()) {
                settings_.skyboxTexture = resources.getOrRegister(js["skyboxTexture"].get<std::string>(), AssetType::Texture);
            }
        }
        if (js.contains("skyboxExposure")) settings_.skyboxExposure = js["skyboxExposure"].get<float>();
        if (js.contains("skyboxRotation")) settings_.skyboxRotation = js["skyboxRotation"].get<float>();
        if (js.contains("iblEnabled")) settings_.iblEnabled = js["iblEnabled"].get<bool>();
        if (js.contains("iblDiffuseIntensity")) settings_.iblDiffuseIntensity = js["iblDiffuseIntensity"].get<float>();
        if (js.contains("iblSpecularIntensity")) settings_.iblSpecularIntensity = js["iblSpecularIntensity"].get<float>();
        if (js.contains("aoEnabled")) settings_.aoEnabled = js["aoEnabled"].get<bool>();
        if (js.contains("aoRadius")) settings_.aoRadius = js["aoRadius"].get<float>();
        if (js.contains("aoIntensity")) settings_.aoIntensity = js["aoIntensity"].get<float>();
        if (js.contains("aoPower")) settings_.aoPower = js["aoPower"].get<float>();
        if (js.contains("fogEnabled")) settings_.fogEnabled = js["fogEnabled"].get<bool>();
        if (js.contains("fogColor")) settings_.fogColor = glm::vec4(jsonToVec3(js["fogColor"], glm::vec3(settings_.fogColor)), 1.0f);
        if (js.contains("fogStart")) settings_.fogStart = js["fogStart"].get<float>();
        if (js.contains("fogDensity")) settings_.fogDensity = js["fogDensity"].get<float>();
        if (js.contains("bloomEnabled")) settings_.bloomEnabled = js["bloomEnabled"].get<bool>();
        if (js.contains("bloomThreshold")) settings_.bloomThreshold = js["bloomThreshold"].get<float>();
        if (js.contains("bloomIntensity")) settings_.bloomIntensity = js["bloomIntensity"].get<float>();
        if (js.contains("bloomRadius")) settings_.bloomRadius = js["bloomRadius"].get<float>();
        if (js.contains("changeRenderingAtLoad")) settings_.changeRenderingAtLoad = js["changeRenderingAtLoad"].get<bool>();
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
                if (bj.contains("iblEnabled")) settings_.iblEnabled = bj["iblEnabled"].get<bool>();
                if (bj.contains("iblDiffuseIntensity")) settings_.iblDiffuseIntensity = bj["iblDiffuseIntensity"].get<float>();
                if (bj.contains("iblSpecularIntensity")) settings_.iblSpecularIntensity = bj["iblSpecularIntensity"].get<float>();
                if (bj.contains("aoEnabled")) settings_.aoEnabled = bj["aoEnabled"].get<bool>();
                if (bj.contains("aoRadius")) settings_.aoRadius = bj["aoRadius"].get<float>();
                if (bj.contains("aoIntensity")) settings_.aoIntensity = bj["aoIntensity"].get<float>();
                if (bj.contains("aoPower")) settings_.aoPower = bj["aoPower"].get<float>();
                if (bj.contains("fogEnabled")) settings_.fogEnabled = bj["fogEnabled"].get<bool>();
                if (bj.contains("fogColor")) settings_.fogColor = glm::vec4(jsonToVec3(bj["fogColor"], glm::vec3(settings_.fogColor)), 1.0f);
                if (bj.contains("fogStart")) settings_.fogStart = bj["fogStart"].get<float>();
                if (bj.contains("fogDensity")) settings_.fogDensity = bj["fogDensity"].get<float>();
                if (bj.contains("bloomEnabled")) settings_.bloomEnabled = bj["bloomEnabled"].get<bool>();
                if (bj.contains("bloomThreshold")) settings_.bloomThreshold = bj["bloomThreshold"].get<float>();
                if (bj.contains("bloomIntensity")) settings_.bloomIntensity = bj["bloomIntensity"].get<float>();
                if (bj.contains("bloomRadius")) settings_.bloomRadius = bj["bloomRadius"].get<float>();
            }
        }
    }
}

} // namespace ne
