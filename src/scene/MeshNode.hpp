#pragma once

#include "scene/Node.hpp"

#include <string>
#include <utility>

namespace ne {

class Mesh;
class Material;

// A node that draws a mesh with a material (cf. Godot MeshInstance3D / Unity
// MeshRenderer). Both are shared resources owned by the ResourceManager; the
// node only references them.
class MeshNode : public Node {
public:
    MeshNode(std::string name, Mesh* mesh, Material* material)
        : Node(std::move(name)), mesh_(mesh), material_(material) {}

    Mesh* mesh() const override { return mesh_; }
    Material* material() const override { return material_; }

private:
    Mesh* mesh_;
    Material* material_;
};

} // namespace ne
