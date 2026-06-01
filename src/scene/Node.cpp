#include "scene/Node.hpp"

#include <glm/gtc/matrix_transform.hpp>  // GLM_FORCE_* set globally by CMake

#include <utility>

namespace ne {

glm::mat4 Transform::matrix() const {
    glm::mat4 m = glm::translate(glm::mat4(1.0f), position);
    m *= glm::mat4_cast(rotation);
    m = glm::scale(m, scale);
    return m;
}

Node::Node(std::string name) : name_(std::move(name)) {}

Node::~Node() = default;

Node* Node::addChild(std::unique_ptr<Node> child) {
    child->parent_ = this;
    Node* ptr = child.get();
    children_.push_back(std::move(child));
    return ptr;
}

void Node::updateTree(float dt) {
    for (auto& behaviour : behaviours_) {
        if (!behaviour->ready_) {
            behaviour->onReady();
            behaviour->ready_ = true;
        }
        behaviour->onUpdate(dt);
    }
    for (auto& child : children_)
        child->updateTree(dt);
}

void Node::traverse(const glm::mat4& parentWorld,
                    const std::function<void(Node&, const glm::mat4&)>& visit) {
    glm::mat4 world = parentWorld * localMatrix();
    visit(*this, world);
    for (auto& child : children_)
        child->traverse(world, visit);
}

} // namespace ne
