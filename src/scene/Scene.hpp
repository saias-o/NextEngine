#pragma once

#include "scene/Node.hpp"

#include <glm/glm.hpp>

namespace ne {

// Whether lighting is evaluated live every frame, or frozen from a bake.
enum class LightingMode {
    Realtime,  // shadows/lighting recomputed each frame; baking disabled
    Baked,     // static contribution precomputed via "Generate Bake"
};

struct SceneSettings {
    glm::vec4 ambientLight{0.04f, 0.04f, 0.05f, 0.0f};
    glm::vec4 clearColor{0.1f, 0.1f, 0.1f, 1.0f};
    bool enablePostProcessing = true;

    LightingMode lightingMode = LightingMode::Realtime;
    bool baked = false;          // true once a bake has been generated
    bool bakeRequested = false;  // transient: editor asks the renderer to bake
};

class MeshNode;
class LightNode;

// A Scene is simply the root Node of a tree (cf. Godot: a scene is a node).

class Scene : public Node {
public:
    Scene();

    // Invokes updateTree on the root, recursively updating all behaviours.
    void update(float dt);

    SceneSettings& settings() { return settings_; }
    const SceneSettings& settings() const { return settings_; }

    const std::vector<MeshNode*>& meshes() const { return meshes_; }
    const std::vector<LightNode*>& lights() const { return lights_; }

private:
    void flattenHierarchy();

    SceneSettings settings_;
    uint32_t lastHierarchyVersion_ = 0;

    std::vector<MeshNode*> meshes_;
    std::vector<LightNode*> lights_;
    std::vector<Behaviour*> flatBehaviours_;
};

} // namespace ne
