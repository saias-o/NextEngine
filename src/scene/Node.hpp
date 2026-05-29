#pragma once

#include "scene/Behaviour.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace ne {

class Mesh;
class LightNode;

// Local transform: translation, rotation (quaternion) and scale.
struct Transform {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};  // identity (w, x, y, z)
    glm::vec3 scale{1.0f};

    glm::mat4 matrix() const;  // local TRS matrix
};

// A node in the scene hierarchy (à la Godot/Unity). Owns its children; a child's
// world transform is its parent's world transform composed with its own local one.
// Nodes are data-only — rendering lives in the Engine, which queries mesh().
class Node {
public:
    explicit Node(std::string name = "Node");
    virtual ~Node();
    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;

    // Takes ownership of `child`, sets its parent, returns a borrowed pointer.
    Node* addChild(std::unique_ptr<Node> child);

    // Convenience: construct a child of type T in place and return it.
    template <typename T, typename... Args>
    T* createChild(Args&&... args) {
        static_assert(std::is_base_of_v<Node, T>, "T must derive from Node");
        auto child = std::make_unique<T>(std::forward<Args>(args)...);
        T* ptr = child.get();
        addChild(std::move(child));
        return ptr;
    }

    // Attach a behaviour of type T, constructed in place; returns it (borrowed).
    template <typename T, typename... Args>
    T* addBehaviour(Args&&... args) {
        static_assert(std::is_base_of_v<Behaviour, T>, "T must derive from Behaviour");
        auto behaviour = std::make_unique<T>(std::forward<Args>(args)...);
        behaviour->node_ = this;
        T* ptr = behaviour.get();
        behaviours_.push_back(std::move(behaviour));
        return ptr;
    }

    // Run behaviour lifecycle (onReady once, then onUpdate) on this node and,
    // recursively, all descendants.
    void updateTree(float dt);

    Transform& transform() { return transform_; }
    const Transform& transform() const { return transform_; }
    glm::mat4 localMatrix() const { return transform_.matrix(); }

    const std::string& name() const { return name_; }
    Node* parent() const { return parent_; }
    const std::vector<std::unique_ptr<Node>>& children() const { return children_; }

    // Depth-first traversal. `visit` receives each node and its world matrix.
    void traverse(const glm::mat4& parentWorld,
                  const std::function<void(Node&, const glm::mat4& world)>& visit);

    // Convenience: traverse treating this node as a root (identity parent).
    void traverse(const std::function<void(Node&, const glm::mat4& world)>& visit) {
        traverse(glm::mat4(1.0f), visit);
    }

    // Type hooks: a node exposes itself as the relevant kind (null otherwise).
    // Kept as cheap virtuals (no RTTI) since they run during scene traversal.
    virtual Mesh* mesh() const { return nullptr; }
    virtual LightNode* asLight() { return nullptr; }

protected:
    std::string name_;
    Transform transform_;
    Node* parent_ = nullptr;
    std::vector<std::unique_ptr<Node>> children_;
    std::vector<std::unique_ptr<Behaviour>> behaviours_;
};

} // namespace ne
