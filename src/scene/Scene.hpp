#pragma once

#include "scene/Node.hpp"

namespace ne {

// A Scene is simply the root Node of a tree (cf. Godot: a scene is a node).
// It inherits the full Node API — children, transform, behaviours, traverse(),
// updateTree() — and exists mainly to name that root and carry scene-wide state
// later on (environment, etc.).
class SceneSettingsBehaviour;

class Scene : public Node {
public:
    Scene();

    // Invokes updateTree on the root, recursively updating all behaviours.
    void update(float dt);

    // Get the active settings (highest in the hierarchy)
    SceneSettingsBehaviour* getActiveSettings();
};

} // namespace ne
