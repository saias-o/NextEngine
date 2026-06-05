#include "scene/Scene.hpp"
#include "scene/Behaviour.hpp"
#include "scene/MeshNode.hpp"
#include "scene/LightNode.hpp"

namespace ne {

Scene::Scene() : Node("Scene") {
}

void Scene::update(float dt) {
    if (lastHierarchyVersion_ != g_hierarchyVersion) {
        flattenHierarchy();
        lastHierarchyVersion_ = g_hierarchyVersion;
    }

    for (auto* b : flatBehaviours_) {
        if (!b->ready_) {
            b->onReady();
            b->ready_ = true;
        }
        b->onUpdate(dt);
    }

    updateTransforms(glm::mat4(1.0f), false);
}

void Scene::flattenHierarchy() {
    meshes_.clear();
    lights_.clear();
    flatBehaviours_.clear();

    traverse([this](Node& n, const glm::mat4&) {
        if (n.mesh()) {
            meshes_.push_back(static_cast<MeshNode*>(&n));
        }
        if (n.asLight()) {
            lights_.push_back(static_cast<LightNode*>(&n));
        }
        for (auto& b : n.behaviours()) {
            flatBehaviours_.push_back(b.get());
        }
    });
}

} // namespace ne
