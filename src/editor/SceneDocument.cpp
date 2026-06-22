#include "editor/SceneDocument.hpp"

#include "scene/Node.hpp"
#include "scene/Scene.hpp"

namespace ne {

void SceneDocument::bind(Scene* scene, ResourceManager* resources) {
    scene_ = scene;
    resources_ = resources;
    if (selectedId_ != kNodeInvalid && !find(selectedId_)) clearSelection();
}

Node* SceneDocument::find(NodeId id) const {
    if (!scene_ || id == kNodeInvalid) return nullptr;
    Node* result = nullptr;
    scene_->traverse([&](Node& node, const glm::mat4&) {
        if (!result && node.id() == id) result = &node;
    });
    return result;
}

void SceneDocument::select(Node* node) {
    selectedId_ = node ? node->id() : kNodeInvalid;
}

void SceneDocument::markLoaded() {
    dirty_ = false;
    clearSelection();
}

} // namespace ne
