#pragma once

#include "core/Camera.hpp"

#include <memory>

namespace ne {

class Window;
class VulkanDevice;
class Swapchain;
class ResourceManager;
class Scene;
class LightNode;
class ImGuiLayer;
class Renderer;
class EditorUI;
class Project;

// Top-level application object. Owns every subsystem and drives the main loop
// (input, scene update, UI, present). Rendering is delegated to the Renderer;
// the Engine keeps orchestration. Declaration order of the members matters:
// device_ outlives the GPU resources that reference it during destruction.
class Engine {
public:
    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    void run();

private:
    void buildScene();
    void updateCursorCapture(bool isPlayMode);
    void processInput(float dt);

    std::unique_ptr<Window> window_;
    std::unique_ptr<VulkanDevice> device_;
    std::unique_ptr<Swapchain> swapchain_;
    std::unique_ptr<ResourceManager> resources_;
    std::unique_ptr<Scene> scene_;
    std::unique_ptr<ImGuiLayer> imgui_;
    std::unique_ptr<Renderer> renderer_;
    std::unique_ptr<EditorUI> editorUI_;
    std::unique_ptr<Project> project_;
    LightNode* sun_ = nullptr;
    LightNode* lamp_ = nullptr;

    Camera camera_;
    bool wasPlayMode_ = false;  // edge-detect play-mode enter/exit for cursor
};

} // namespace ne
