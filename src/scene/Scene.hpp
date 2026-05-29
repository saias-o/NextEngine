#pragma once

#include "scene/Node.hpp"

#include <glm/glm.hpp>

#include <functional>
#include <memory>

namespace ne {

// Holds the root of a node hierarchy (cf. Godot SceneTree). Thin owner that
// exposes the root and a convenience traversal from world origin.
class Scene {
public:
    Scene() : root_(std::make_unique<Node>("root")) {}

    Node& root() { return *root_; }

    void traverse(const std::function<void(Node&, const glm::mat4& world)>& visit) {
        root_->traverse(glm::mat4(1.0f), visit);
    }

private:
    std::unique_ptr<Node> root_;
};

} // namespace ne
