#pragma once

#include "scene/Node.hpp"

namespace ne {

// A Scene is simply the root Node of a tree (cf. Godot: a scene is a node).
// It inherits the full Node API — children, transform, behaviours, traverse(),
// updateTree() — and exists mainly to name that root and carry scene-wide state
// later on (environment, etc.).
class Scene : public Node {
public:
    Scene() : Node("Scene") {}
};

} // namespace ne
