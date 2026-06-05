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
    MeshNode() : Node("MeshNode"), mesh_(nullptr), material_(nullptr) {}
    MeshNode(std::string name, Mesh* mesh, Material* material)
        : Node(std::move(name)), mesh_(mesh), material_(material) {}

    const char* typeName() const override { return "MeshNode"; }
    void serialize(nlohmann::json& j, ResourceManager& resources) const override;
    void deserialize(const nlohmann::json& j, ResourceManager& resources) override;

    Mesh* mesh() const override { return mesh_; }
    Material* material() const override { return material_; }

    void setMesh(Mesh* mesh) { mesh_ = mesh; }
    void setMaterial(Material* material) { material_ = material; }

    bool& castShadows() { return castShadows_; }
    bool castShadows() const { return castShadows_; }

    bool& includeInLightBaking() { return includeInLightBaking_; }
    bool includeInLightBaking() const { return includeInLightBaking_; }

    bool meshEnabled() const { return meshEnabled_; }
    void setMeshEnabled(bool enabled) {
        if (meshEnabled_ != enabled) {
            meshEnabled_ = enabled;
            g_hierarchyVersion++;
        }
    }

private:
    Mesh* mesh_;
    Material* material_;
    bool castShadows_ = true;
    bool includeInLightBaking_ = false;
    bool meshEnabled_ = true;
};

} // namespace ne
