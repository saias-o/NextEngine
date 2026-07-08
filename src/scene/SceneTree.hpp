#pragma once

#include "scene/Node.hpp"  // for the autoload<T>() template (Node::getBehaviour)
#include "core/Easing.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace saida {

using TimerId = uint64_t;

class Scene;
class ResourceManager;
class Behaviour;

// The runtime context of a running game (cf. Godot SceneTree). Owns a persistent
// "World" root that hosts the current gameplay sub-scene + the autoloads. Scene
// changes only swap the sub-scene; the World and autoloads survive.
//
// Mounted only at runtime/Play. Behaviours reach it via `node()->tree()`.
class SceneTree {
public:
    explicit SceneTree(ResourceManager& resources);
    ~SceneTree();
    SceneTree(const SceneTree&) = delete;
    SceneTree& operator=(const SceneTree&) = delete;

    // Build the World, adopting `startScene` (the LIVE edit scene, moved — never
    // copied, so live resources like WebCanvas views aren't duplicated) as the
    // current sub-scene. Returns the World (the render/update target). Spawns autoloads.
    Scene* mountWorld(std::unique_ptr<Scene> startScene);
    void unmountWorld();
    bool mounted() const { return world_ != nullptr; }
    Scene& world() { return *world_; }
    Scene& currentScene();  // the swappable gameplay sub-scene (or the World if none)

    // Project root used to resolve relative .scene paths (set by the Engine).
    void setProjectRoot(const std::string& root) { projectRoot_ = root; }
    std::string resolveProjectPath(const std::string& path) const;

    // Load a .scene and add it under `parent` (or the current sub-scene if null);
    // returns the new node (its transform can then be set). Cached after first
    // load, so spawning many copies (projectiles, enemies) avoids disk I/O.
    Node* instantiate(const std::string& scenePath, Node* parent = nullptr);

    void changeScene(const std::string& scenePath);  // load a level file
    void reloadScene();
    void requestFree(Node* node);                     // backs Node::queueFree()
    void applyDeferred();
    bool quitRequested() const { return quitRequested_; }

    void setPaused(bool paused);   // drives Time::setScale(0/1)
    bool paused() const;
    void quit() { quitRequested_ = true; }

    TimerId after(Node* owner, float seconds, std::function<void()> fn);    // one-shot
    TimerId every(Node* owner, float interval, std::function<void()> fn);   // repeating
    TimerId tween(Node* owner, float duration, Easing easing,
                  std::function<void(float)> fn);  // fn(eased t) each frame until done
    TimerId after(Behaviour* owner, float seconds, std::function<void()> fn);
    TimerId every(Behaviour* owner, float interval, std::function<void()> fn);
    TimerId tween(Behaviour* owner, float duration, Easing easing,
                  std::function<void(float)> fn);
    void cancelTimer(TimerId id);
    void cancelTimersOwnedBy(Node* owner);
    void tickTimers(float dt);  // called by the Engine each frame (scaled dt)

    // Code-registered: a node carrying a behaviour T.
    template <typename T>
    void registerAutoload(const std::string& name) {
        setAutoloadDef(name, [name] {
            auto node = std::make_unique<Node>(name);
            node->addBehaviour<T>();
            return node;
        });
    }
    // Data-driven: a registered behaviour type by name, or a prefab ".scene" file.
    void registerAutoloadType(const std::string& name, const std::string& behaviourType);
    void registerAutoloadScene(const std::string& name, const std::string& scenePath);

    // Reach an autoload's behaviour (the sanctioned global-state accessor).
    template <typename T>
    T* autoload() const {
        // `template` requis : GCC 12 traite les structured bindings comme
        // type-dépendants dans un template (corrigé en GCC 13).
        for (const auto& [name, node] : autoloadNodes_)
            if (T* b = node->template getBehaviour<T>()) return b;
        return nullptr;
    }
    Node* autoloadNode(const std::string& name) const;

    // Locate nodes by opt-in tag (the only global lookup; there is no find-by-name).
    // Traversal-based — always correct across reparenting/destruction.
    const std::vector<Node*>& group(const std::string& name);
    Node* firstInGroup(const std::string& name);

    // Character bodies (CharacterVirtual: player, NPCs, cars) are NOT visible to
    // PhysicsWorld::raycast, so gameplay (weapons, AoE, aggro, pickups, run-overs)
    // would otherwise re-implement the same manual loops. These node-level queries
    // close that gap: each node is approximated by a sphere at its world origin.
    // Pass a non-empty `group` to restrict candidates (cheap and usually what you
    // want); empty scans the whole tree.

    struct NodeRayHit {
        Node* node = nullptr;   // null when nothing was hit
        float distance = 0.0f;  // along the ray
        glm::vec3 point{0.0f};
    };

    // Nodes whose world origin lies within `radius` of `center`.
    std::vector<Node*> overlapSphere(const glm::vec3& center, float radius,
                                     const std::string& group = "");

    // Nearest node along the ray, each treated as a sphere of `nodeRadius` about
    // its world origin. Returns {nullptr} when nothing is in range.
    NodeRayHit raycastNodes(const glm::vec3& origin, const glm::vec3& direction,
                            float maxDistance, float nodeRadius,
                            const std::string& group = "");

private:
    using AutoloadFactory = std::function<std::unique_ptr<Node>()>;
    void setAutoloadDef(const std::string& name, AutoloadFactory factory);  // dedup by name
    std::string resolvePath(const std::string& path) const;  // relative → project-absolute
    void loadCurrentScene(std::unique_ptr<Node> sceneNode);
    void applyLevelSettings(Scene& level);  // copy settings to World if flagged
    void startAutoloads();
    void clearAutoloads();

    ResourceManager& resources_;
    std::unique_ptr<Scene> world_;
    Scene* currentScene_ = nullptr;
    std::string currentScenePath_;

    // deferred state
    bool pendingChange_ = false;
    std::string pendingScenePath_;
    std::vector<Node*> pendingFree_;
    bool quitRequested_ = false;

    // autoloads
    struct AutoloadDef {
        std::string name;
        AutoloadFactory factory;
    };
    std::vector<AutoloadDef> autoloadDefs_;            // survive scene swaps
    std::unordered_map<std::string, Node*> autoloadNodes_;  // spawned (children of World)

    std::vector<Node*> groupBuffer_;  // reusable result buffer for group()

    std::string projectRoot_;
    std::unordered_map<std::string, std::string> sceneCache_;  // path -> node JSON (instancing)

    // timers
    struct Timer {
        TimerId id;
        Node* nodeOwner;
        Behaviour* behaviourOwner;
        enum Kind { After, Every, Tween } kind;
        float time = 0.0f;       // accumulated
        float duration;          // delay / interval / tween length
        Easing easing = Easing::Linear;
        std::function<void()> fn;
        std::function<void(float)> tweenFn;
        bool dead = false;
    };
    std::vector<Timer> timers_;
    TimerId nextTimerId_ = 0;

    TimerId addTimer(Timer t);
};

} // namespace saida
