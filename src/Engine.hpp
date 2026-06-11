#pragma once

#include "core/Camera.hpp"
#include "ui/UIInteractionSystem.hpp"

#include <functional>
#include <memory>
#include <string>

namespace ne {

class Window;
class VulkanDevice;
class Swapchain;
class ResourceManager;
class Scene;
class SceneTree;
class ImGuiLayer;
class Renderer;
class Project;
class WebEngine;

// How a game populates the scene at startup. Provided by the executable (game
// code), so the engine library has no built-in content.
using SceneSetup = std::function<void(Scene&, ResourceManager&)>;

// Runtime: owns the window, GPU, scene, renderer and the loop. It is
// editor-agnostic — an application layer (editor or game) drives it via an
// onFrame() callback invoked each frame between the ImGui begin/end, and reads
// its state through the accessors. Declaration order of members matters:
// device_ outlives the GPU resources that reference it during destruction.
class Engine {
public:
    explicit Engine(SceneSetup sceneSetup, const std::string& initialProject = "");
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Per-frame application hook (UI + its own input), called inside the ImGui
    // frame, before the scene behaviours update and the render.
    using FrameFn = std::function<void(float dt)>;
    void setOnFrame(FrameFn fn) { onFrame_ = std::move(fn); }

    void run();

    void setSceneOverride(Scene* scene) { sceneOverride_ = scene; }

    // Runtime/Play: wrap the current edit scene in the persistent World (hosting
    // autoloads + the swappable gameplay sub-scene) and render that instead.
    void mountWorld();
    void unmountWorld();

    // Accessors for the application layer.
    Scene& scene() { return *scene_; }
    SceneTree& sceneTree() { return *sceneTree_; }
    Camera& camera() { return camera_; }
    ResourceManager& resources() { return *resources_; }
    Project& project() { return *project_; }
    Window& window() { return *window_; }

private:
    std::unique_ptr<Window> window_;
    std::unique_ptr<VulkanDevice> device_;
    std::unique_ptr<Swapchain> swapchain_;
    std::unique_ptr<ResourceManager> resources_;

    UIInteractionSystem uiInteraction_;

    std::unique_ptr<Scene> scene_;
    Scene* sceneOverride_ = nullptr;
    std::unique_ptr<SceneTree> sceneTree_;
    std::unique_ptr<ImGuiLayer> imgui_;
    std::unique_ptr<Renderer> renderer_;
    std::unique_ptr<Project> project_;

    FrameFn onFrame_;
    Camera camera_;

    std::string playSnapshot_;  // edit doc serialized at play start, to restore on stop
};

} // namespace ne
