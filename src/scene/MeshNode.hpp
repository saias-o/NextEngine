#pragma once

#include "scene/Node.hpp"

#include <string>
#include <utility>

namespace ne {

class Mesh;

// A node that draws a mesh (cf. Godot MeshInstance3D / Unity MeshRenderer).
// The mesh is a shared resource owned elsewhere (the Engine); the node only
// references it.
class MeshNode : public Node {
public:
    MeshNode(std::string name, Mesh* mesh)
        : Node(std::move(name)), mesh_(mesh) {}

    Mesh* mesh() const override { return mesh_; }

private:
    Mesh* mesh_;
};

} // namespace ne
