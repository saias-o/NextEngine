#include "scene/Node.hpp"

#include <glm/gtc/matrix_transform.hpp>  // GLM_FORCE_* set globally by CMake

#include <utility>

namespace ne {

uint32_t Node::g_hierarchyVersion = 1;

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
    g_hierarchyVersion++;
    return ptr;
}

Behaviour* Node::addBehaviour(std::unique_ptr<Behaviour> behaviour) {
    behaviour->node_ = this;
    Behaviour* ptr = behaviour.get();
    behaviours_.push_back(std::move(behaviour));
    return ptr;
}

bool Node::removeChild(Node* child) {
    if (!child) return false;
    for (auto it = children_.begin(); it != children_.end(); ++it) {
        if (it->get() == child) {
            children_.erase(it);
            g_hierarchyVersion++;
            return true;
        }
    }
    return false;
}

std::unique_ptr<Node> Node::detachChild(Node* child) {
    if (!child) return nullptr;
    for (auto it = children_.begin(); it != children_.end(); ++it) {
        if (it->get() == child) {
            std::unique_ptr<Node> detached = std::move(*it);
            children_.erase(it);
            detached->parent_ = nullptr;
            g_hierarchyVersion++;
            return detached;
        }
    }
    return nullptr;
}

void Node::setEnabled(bool enabled) {
    if (enabled_ != enabled) {
        enabled_ = enabled;
        g_hierarchyVersion++;
    }
}

bool Node::isActiveInHierarchy() const {
    if (!enabled_) return false;
    if (parent_) return parent_->isActiveInHierarchy();
    return true;
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

void Node::updateTransforms(const glm::mat4& parentWorld, bool parentDirty) {
    glm::mat4 currentLocal = localMatrix();
    bool dirty = parentDirty || (currentLocal != lastLocalMatrix_);
    
    if (dirty) {
        worldTransform_ = parentWorld * currentLocal;
        lastLocalMatrix_ = currentLocal;
    }
    
    for (auto& child : children_) {
        child->updateTransforms(worldTransform_, dirty);
    }
}

void Node::traverse(const glm::mat4& parentWorld,
                    const std::function<void(Node&, const glm::mat4&)>& visit) {
    glm::mat4 world = parentWorld * localMatrix();
    visit(*this, world);
    for (auto& child : children_)
        child->traverse(world, visit);
}

} // namespace ne
