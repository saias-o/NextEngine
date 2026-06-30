#pragma once

#include "scene/NodeId.hpp"

#include <cstdint>
#include <string>

namespace saida {

class Node;
class Scene;
class ResourceManager;

class SceneDocument {
public:
    void bind(Scene* scene, ResourceManager* resources);

    Scene* scene() const { return scene_; }
    ResourceManager* resources() const { return resources_; }
    Node* find(NodeId id) const;

    void select(Node* node);
    void clearSelection() { selectedId_ = kNodeInvalid; }
    NodeId selectedId() const { return selectedId_; }
    Node* selectedNode() const { return find(selectedId_); }

    bool dirty() const { return dirty_; }
    void markDirty() { dirty_ = true; }
    void markSaved() { dirty_ = false; }
    void markLoaded();  // clears dirty + selection

private:
    Scene* scene_ = nullptr;
    ResourceManager* resources_ = nullptr;
    NodeId selectedId_ = kNodeInvalid;
    bool dirty_ = false;
};

} // namespace saida
