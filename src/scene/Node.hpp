#pragma once

#include "scene/Behaviour.hpp"
#include "scene/NodeId.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace ne {

class Mesh;
class Material;
class LightNode;
class CollisionObjectNode;
class CharacterBodyNode;
class ResourceManager;
class SceneTree;

// Local transform: translation, rotation (quaternion) and scale.
struct Transform {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};  // identity (w, x, y, z)
    glm::vec3 scale{1.0f};

    glm::mat4 matrix() const;  // local TRS matrix
};

// Scene hierarchy node. Owns children; world transform composes parent and local.
class Node {
public:
    explicit Node(std::string name = "Node");
    virtual ~Node();
    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;

    // Takes ownership of `child`, sets its parent, returns a borrowed pointer.
    Node* addChild(std::unique_ptr<Node> child);

    // Takes ownership of `child`, inserts it at `index`, returns a borrowed pointer.
    Node* addChildAt(std::unique_ptr<Node> child, size_t index);

    // Erases and deletes `child` if it belongs to this node.
    bool removeChild(Node* child);

    // Detach a child from this node and return ownership. Returns nullptr if not a child.
    std::unique_ptr<Node> detachChild(Node* child);


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
        return static_cast<T*>(addBehaviour(std::make_unique<T>(std::forward<Args>(args)...)));
    }

    // Attach an already-constructed behaviour (e.g. from the BehaviourRegistry
    // during deserialization); returns it (borrowed).
    Behaviour* addBehaviour(std::unique_ptr<Behaviour> behaviour);
    Behaviour* addBehaviourAt(std::unique_ptr<Behaviour> behaviour, size_t index);

    // Remove a specific behaviour instance.
    void removeBehaviour(Behaviour* b);

    // Remove all children (and, transitively, their subtrees).
    void clearChildren() { children_.clear(); }

    // Run behaviour lifecycle (onReady once, then onUpdate) on this node and,
    // recursively, all descendants.
    void updateTree(float dt);

    void updateTransforms(const glm::mat4& parentWorld, bool parentDirty);

    const glm::mat4& worldTransform() const { return worldTransform_; }

    // Runtime SceneTree, or nullptr when not under a live World.
    SceneTree* tree() const;

    // Request safe removal of this node at end of frame (never destroy mid-update).
    void queueFree();

    // Overridden by the World Scene to expose its tree; default none.
    virtual SceneTree* ownTree() const { return nullptr; }

    Transform& transform() { return transform_; }
    const Transform& transform() const { return transform_; }
    glm::mat4 localMatrix() const { return transform_.matrix(); }

    const std::string& name() const { return name_; }
    void setName(const std::string& name) { name_ = name; }
    NodeId id() const { return id_; }
    void assignSerializedId(NodeId id) { id_ = id != kNodeInvalid ? id : generateNodeId(); }
    void regenerateId() { id_ = generateNodeId(); }
    
    bool enabled() const { return enabled_; }
    void setEnabled(bool enabled);
    bool isActiveInHierarchy() const;

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
    virtual Material* material() const { return nullptr; }
    virtual LightNode* asLight() { return nullptr; }
    virtual const LightNode* asLightConst() const { return nullptr; }
    virtual CollisionObjectNode* asCollisionObject() { return nullptr; }
    virtual CharacterBodyNode* asCharacterBody() { return nullptr; }

    virtual const char* typeName() const { return "Node"; }
    virtual void serialize(nlohmann::json& j, ResourceManager& resources) const;
    virtual void deserialize(const nlohmann::json& j, ResourceManager& resources);

    // Behaviour queries (used by the editor inspector).
    bool hasBehaviours() const { return !behaviours_.empty(); }
    int behaviourCount() const { return static_cast<int>(behaviours_.size()); }
    const std::vector<std::unique_ptr<Behaviour>>& behaviours() const { return behaviours_; }

    template<typename T>
    T* getBehaviour() const {
        for (auto& b : behaviours_) {
            if (auto* ptr = dynamic_cast<T*>(b.get())) {
                return ptr;
            }
        }
        return nullptr;
    }

    // Return a behaviour of type T, adding one if absent (cf. Unity RequireComponent).
    // Declares a behaviour's dependency on a sibling explicitly. Call in onReady().
    template<typename T>
    T* requireBehaviour() {
        if (T* b = getBehaviour<T>()) return b;
        return addBehaviour<T>();
    }

    // Scoped queries — DESCENDANTS only ("call down"). The clean alternative to a
    // global find-by-name (which intentionally does not exist).
    template<typename T>
    T* findBehaviourInChildren() const {
        for (const auto& c : children_) {
            if (T* b = c->getBehaviour<T>()) return b;
            if (T* b = c->findBehaviourInChildren<T>()) return b;
        }
        return nullptr;
    }
    template<typename T>
    T* getChildNode() const {
        for (const auto& c : children_)
            if (T* n = dynamic_cast<T*>(c.get())) return n;
        return nullptr;
    }

    // Groups (tags) — opt-in membership used by SceneTree::group()/firstInGroup()
    // to locate nodes (e.g. the player) without coupling by name.
    void addToGroup(const std::string& group);
    void removeFromGroup(const std::string& group);
    bool isInGroup(const std::string& group) const;
    const std::vector<std::string>& groups() const { return groups_; }

    // Optional metadata: path to the file this node was imported from (e.g. .glb)
    const std::string& importedFromPath() const { return importedFromPath_; }
    void setImportedFromPath(const std::string& path) { importedFromPath_ = path; }

protected:
    NodeId id_ = kNodeInvalid;
    std::string name_;
    std::string importedFromPath_;
    bool enabled_ = true;
    Transform transform_;
    Node* parent_ = nullptr;
    std::vector<std::unique_ptr<Node>> children_;
    std::vector<std::unique_ptr<Behaviour>> behaviours_;
    std::vector<std::string> groups_;

    glm::mat4 worldTransform_{1.0f};
    glm::mat4 lastLocalMatrix_{0.0f};

public:
    static uint32_t g_hierarchyVersion;
    static uint32_t g_transformVersion;
};

} // namespace ne
