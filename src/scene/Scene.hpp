#pragma once

#include "scene/Node.hpp"
#include "project/AssetRegistry.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <vector>

namespace ne {

class PhysicsWorld;
class CollisionObjectNode;

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

    // Global illumination (DDGI irradiance volume — the single GI primitive).
    bool giEnabled = true;       // sample the irradiance volume for indirect diffuse
    bool giDebugVoxels = false;  // debug: visualize the voxelized scene albedo
    float giIntensity = 1.0f;    // indirect diffuse multiplier (boost to make GI visible)

    AssetID skyboxTexture = kAssetInvalid;
    float skyboxExposure = 1.0f;
    float skyboxRotation = 0.0f;

    // Debug: draw animated skeletons as bone lines (editor tool; desktop only).
    bool showSkeletons = false;

    // When a sub-scene with this flag is loaded into the persistent World, its
    // rendering settings (above) override the World's. If false, the World keeps
    // the settings it already has (those of the "super-scene" / a previous level).
    bool changeRenderingAtLoad = true;
};

class MeshNode;
class LightNode;
class SceneTree;

// A Scene is simply the root Node of a tree (cf. Godot: a scene is a node).

class Scene : public Node {
public:
    Scene();
    ~Scene() override;

    const char* typeName() const override { return "Scene"; }
    void serialize(nlohmann::json& j, ResourceManager& resources) const override;
    void deserialize(const nlohmann::json& j, ResourceManager& resources) override;

    // Invokes updateTree on the root, recursively updating all behaviours.
    void update(float dt);

    SceneSettings& settings() { return settings_; }
    const SceneSettings& settings() const { return settings_; }

    const std::vector<MeshNode*>& meshes() const { return meshes_; }
    const std::vector<LightNode*>& lights() const { return lights_; }

    // The per-scene physics world (created lazily once a body exists; null until then).
    PhysicsWorld* physics() const { return physics_.get(); }

    // SceneTree wiring: only the persistent World root carries a tree pointer;
    // Node::tree() walks to the root and reads it via ownTree().
    void setTree(SceneTree* t) { tree_ = t; }
    SceneTree* ownTree() const override { return tree_; }

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
    std::vector<CollisionObjectNode*> bodies_;

    std::unique_ptr<PhysicsWorld> physics_;
    SceneTree* tree_ = nullptr;
};

} // namespace ne
