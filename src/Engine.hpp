#pragma once

#include "core/Camera.hpp"
#include "render/CameraDirector.hpp"
#include "ui/UIInteractionSystem.hpp"

#include <functional>
#include <memory>
#include <string>

namespace saida {

class Window;
class VulkanDevice;
class Swapchain;
class ResourceManager;
class Scene;
class SceneTree;
class ImGuiLayer;
class Renderer;
class Project;
#ifdef SAIDA_ENABLE_XR
namespace xr { class Instance; class Session; }
class VulkanDeviceCreator;
#endif

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
    explicit Engine(SceneSetup sceneSetup, const std::string& initialProject = "",
                    bool requireXr = false);
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Per-frame application hook (UI + its own input), called inside the ImGui
    // frame, before the scene behaviours update and the render.
    using FrameFn = std::function<void(float dt)>;
    void setOnFrame(FrameFn fn) { onFrame_ = std::move(fn); }

    void run();

    // Runs exactly one frame (input, scene update, UI, render). Returns false
    // once the window wants to close. runDesktop() loops on it; the web driver
    // (Étape 16.4) calls it from the browser's requestAnimationFrame — a
    // blocking while(true) is forbidden under Emscripten (PLAN_WEB_EXPORT §4.5).
    bool tick();

    // True only in the explicit XR runtime process, which drives an OpenXR
    // session (head-tracked stereo) instead of the desktop editor window.
    bool xrMode() const { return xrMode_; }

    void setSceneOverride(Scene* scene) { sceneOverride_ = scene; }
    void setRenderViewport(glm::vec2 position, glm::vec2 size);
    void clearRenderViewport();

    // Runtime/Play: wrap the current edit scene in the persistent World (hosting
    // autoloads + the swappable gameplay sub-scene) and render that instead.
    void mountWorld();
    void unmountWorld();

#ifdef SAIDA_ENABLE_XR
    // Returns true and launches an isolated XR preview process when the scene
    // contains XR nodes that require OpenXR presentation. The editor calls this
    // before mountWorld() so the desktop editor never tries to drive OpenXR.
    bool launchExternalPreviewIfNeeded();
#endif

    // Accessors for the application layer.
    Scene& scene() { return *scene_; }
    SceneTree& sceneTree() { return *sceneTree_; }
    Camera& camera() { return camera_; }
    ResourceManager& resources() { return *resources_; }
    Project& project() { return *project_; }
    Window& window() { return *window_; }

private:
    // Desktop frame loop (window present path) and XR frame loop (OpenXR session).
    void runDesktop();
    double tickLastTime_ = 0.0;
    bool tickWasLeftDown_ = false;
#ifdef SAIDA_ENABLE_XR
    void runXr();
#endif

    std::unique_ptr<Window> window_;
#ifdef SAIDA_ENABLE_XR
    std::unique_ptr<xr::Instance> xrInstance_;
    std::unique_ptr<VulkanDeviceCreator> xrCreator_;
#endif
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
    CameraDirector cameraDirector_;  // picks + blends scene cameras during Play
    bool renderViewportOverride_ = false;
    glm::vec2 renderViewportPos_{0.0f};
    glm::vec2 renderViewportSize_{0.0f};

    std::string playSnapshot_;  // edit doc serialized at play start, to restore on stop

#ifdef SAIDA_ENABLE_XR
    // OpenXR session — declared last so it is destroyed first (before device_ and
    // xrInstance_), since it owns Vulkan command buffers / image views.
    std::unique_ptr<xr::Session> xrSession_;
#endif
    bool xrMode_ = false;
};

} // namespace saida
