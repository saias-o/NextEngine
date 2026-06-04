#include "Engine.hpp"

#include "core/Input.hpp"
#include "core/Log.hpp"
#include "core/Time.hpp"
#include "core/Window.hpp"
#include "graphics/ImGuiLayer.hpp"
#include "graphics/ResourceManager.hpp"
#include "graphics/Swapchain.hpp"
#include "graphics/VulkanDevice.hpp"
#include "project/Project.hpp"
#include "render/Renderer.hpp"
#include "scene/Scene.hpp"

namespace ne {

namespace {
constexpr uint32_t kWidth = 1600;
constexpr uint32_t kHeight = 900;
}

Engine::Engine(SceneSetup sceneSetup, const std::string& initialProject) {
    window_ = std::make_unique<Window>(kWidth, kHeight, "NextEngine");
    Input::bind(window_.get());
    device_ = std::make_unique<VulkanDevice>(*window_);
    swapchain_ = std::make_unique<Swapchain>(*device_, *window_);
    resources_ = std::make_unique<ResourceManager>(*device_);

    scene_ = std::make_unique<Scene>();
    project_ = std::make_unique<Project>();

    if (!initialProject.empty() && !project_->load(initialProject))
        Log::warn("Failed to load initial project: ", initialProject);

    // No project loaded → the game provides its scene.
    if (!project_->isLoaded() && sceneSetup)
        sceneSetup(*scene_, *resources_);

    imgui_ = std::make_unique<ImGuiLayer>(*device_, *window_, swapchain_->renderPass(),
        swapchain_->imageCount(), swapchain_->samples());
    renderer_ = std::make_unique<Renderer>(*device_, *swapchain_, *window_,
        resources_->materialSetLayout(), *imgui_);

    // Default editor/viewport camera placement (the app may move it).
    camera_.position = {0.0f, 2.5f, 8.0f};
    camera_.yaw = -90.0f;
    camera_.pitch = -15.0f;
}

Engine::~Engine() {
    vkDeviceWaitIdle(device_->device());
    // Subsystems are torn down by their unique_ptr destructors, in reverse
    // declaration order, while device_ is still alive (renderer_ before imgui_,
    // swapchain_ and device_).
}

void Engine::run() {
    double last = glfwGetTime();
    while (!window_->shouldClose()) {
        window_->pollEvents();

        double now = glfwGetTime();
        float realDt = static_cast<float>(now - last);
        last = now;

        Time::update(realDt);  // sets scaled delta + elapsed
        Input::newFrame();     // single per-frame input snapshot

        imgui_->beginFrame();
        if (onFrame_)
            onFrame_(realDt);              // application: its input + UI
        scene_->updateTree(Time::delta()); // behaviours: scaled time (pausable)
        imgui_->endFrame();  // finalize draw data even if the frame is skipped (resize)

        renderer_->drawFrame(*scene_, camera_);
    }
    vkDeviceWaitIdle(device_->device());
}

} // namespace ne
