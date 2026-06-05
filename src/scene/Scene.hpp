#pragma once

#include "scene/Node.hpp"
#include "project/AssetRegistry.hpp"

#include <glm/glm.hpp>

namespace ne {

// Whether lighting is evaluated live every frame, or frozen from a bake.
enum class LightingMode {
    Realtime,  // shadows/lighting recomputed each frame; baking disabled
    Baked,     // static contribution precomputed via "Generate Bake"
};

struct SceneSettings {
    glm::vec4 ambientLight{0.1f, 0.1f, 0.1f, 1.0f};
    glm::vec4 clearColor{0.0f, 0.0f, 0.0f, 1.0f};
    bool enablePostProcessing = true;

    LightingMode lightingMode = LightingMode::Realtime;
    bool baked = false;          // true once a bake has been generated
    bool bakeRequested = false;  // transient: editor asks the renderer to bake

    AssetID skyboxTexture = kAssetInvalid;
    float skyboxExposure = 1.0f;
    float skyboxRotation = 0.0f;
};

class MeshNode;
class LightNode;

// A Scene is simply the root Node of a tree (cf. Godot: a scene is a node).

class Scene : public Node {
public:
    Scene();

    const char* typeName() const override { return "Scene"; }
    void serialize(nlohmann::json& j, ResourceManager& resources) const override;
    void deserialize(const nlohmann::json& j, ResourceManager& resources) override;

    // Invokes updateTree on the root, recursively updating all behaviours.
    void update(float dt);

    SceneSettings& settings() { return settings_; }
    const SceneSettings& settings() const { return settings_; }

    const std::vector<MeshNode*>& meshes() const { return meshes_; }
    const std::vector<LightNode*>& lights() const { return lights_; }

    AssetID prefabAssetId() const { return prefabAssetId_; }
    void setPrefabAssetId(AssetID id) { prefabAssetId_ = id; }

private:
    void flattenHierarchy();

    SceneSettings settings_;
    AssetID prefabAssetId_ = kAssetInvalid;
    uint32_t lastHierarchyVersion_ = 0;

    std::vector<MeshNode*> meshes_;
    std::vector<LightNode*> lights_;
    std::vector<Behaviour*> flatBehaviours_;
};

} // namespace ne
