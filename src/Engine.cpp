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
#include "scene/BehaviourRegistry.hpp"
#include "scene/NodeRegistry.hpp"
#include "scene/MeshNode.hpp"
#include "scene/LightNode.hpp"
#include "scene/WebCanvasNode.hpp"
#include "scene/UINode.hpp"
#include "scene/UICanvasNode.hpp"
#include "scene/UIColorNode.hpp"
#include "scene/UIImageNode.hpp"
#include "scene/UITextNode.hpp"
#include "scene/UIInteractableNode.hpp"
#include "scene/UIButtonNode.hpp"
#include "scene/UIToggleNode.hpp"
#include "audio/AudioManager.hpp"
#include "audio/AudioSourceBehaviour.hpp"
#include "scene/CharacterBehaviour.hpp"
#include "ui/WebEngine.hpp"

#include <thread>
#include <chrono>

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

    resources_->setRegistry(&project_->assetRegistry());

    if (!initialProject.empty() && !project_->load(initialProject))
        Log::warn("Failed to load initial project: ", initialProject);

    // No project loaded → the game provides its scene.
    if (!project_->isLoaded() && sceneSetup)
        sceneSetup(*scene_, *resources_);

    AudioManager::get().init();
    
    // Register built-in behaviours
    BehaviourRegistry::instance().registerType<AudioSourceBehaviour>("AudioSource");
    BehaviourRegistry::instance().registerType<CharacterBehaviour>("Character");

    // Register built-in nodes
    NodeRegistry::instance().registerType<Node>("Node");
    NodeRegistry::instance().registerType<Scene>("Scene");
    NodeRegistry::instance().registerType<MeshNode>("MeshNode");
    NodeRegistry::instance().registerType<LightNode>("LightNode");
    NodeRegistry::instance().registerType<WebCanvasNode>("WebCanvasNode");
    NodeRegistry::instance().registerType<UINode>("UINode");
    NodeRegistry::instance().registerType<UICanvasNode>("UICanvasNode");
    NodeRegistry::instance().registerType<UIColorNode>("UIColorNode");
    NodeRegistry::instance().registerType<UIImageNode>("UIImageNode");
    NodeRegistry::instance().registerType<UITextNode>("UITextNode");
    NodeRegistry::instance().registerType<UIInteractableNode>("UIInteractableNode");
    NodeRegistry::instance().registerType<UIButtonNode>("UIButtonNode");
    NodeRegistry::instance().registerType<UIToggleNode>("UIToggleNode");
    if (project_->isLoaded()) {
        AudioManager::get().setProjectRoot(project_->rootPath());
        AudioManager::get().setDefaultSettings(project_->defaultAudioSettings());
        AudioManager::get().setMasterVolume(project_->masterVolume());
    }

    imgui_ = std::make_unique<ImGuiLayer>(*device_, *window_, swapchain_->colorFormat(),
        swapchain_->imageCount(), VK_SAMPLE_COUNT_1_BIT);
    renderer_ = std::make_unique<Renderer>(*device_, *swapchain_, *window_,
        *resources_, *imgui_);

    // Default editor/viewport camera placement (the app may move it).
    camera_.position = {0.0f, 2.5f, 8.0f};
    camera_.yaw = -90.0f;
    camera_.pitch = -15.0f;
}

Engine::~Engine() {
    AudioManager::get().shutdown();
    vkDeviceWaitIdle(device_->device());
    // Subsystems are torn down by their unique_ptr destructors, in reverse
    // declaration order, while device_ is still alive (renderer_ before imgui_,
    // swapchain_ and device_).
}

void Engine::run() {
    double last = glfwGetTime();
    while (!window_->shouldClose()) {
        window_->pollEvents();

        int maxFps = project_ ? project_->maxFps() : Project::kDefaultMaxFps;
        if (maxFps > 0) {
            double targetTime = 1.0 / maxFps;
            while (true) {
                double elapsed = glfwGetTime() - last;
                if (elapsed >= targetTime) break;
                
                // If we have more than 2ms to wait, sleep to save CPU.
                // Otherwise, yield/spin for precision.
                if (targetTime - elapsed > 0.002) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                } else {
                    std::this_thread::yield();
                }
            }
        }
        
        double now = glfwGetTime();
        float realDt = static_cast<float>(now - last);
        last = now;

        Time::update(realDt);  // sets scaled delta + elapsed
        Input::newFrame();     // single per-frame input snapshot

        static bool wasLeftDown = false;
        bool isLeftDown = Input::isMouseButtonDown(MouseButton::Left);
        bool isLeftPressed = Input::isMouseButtonPressed(MouseButton::Left);
        bool isLeftReleased = !isLeftDown && wasLeftDown;
        wasLeftDown = isLeftDown;

        if (uiInteraction_.update(*scene_, Input::mousePosition(), isLeftDown, isLeftPressed, isLeftReleased)) {
            Input::consumeMouse();
        }

        imgui_->beginFrame();
        if (onFrame_)
            onFrame_(realDt);              // application: its input + UI
        WebEngine::get().update();         // met à jour Ultralight et ses bitmaps CPU
        scene_->update(Time::delta());     // behaviours: scaled time (pausable)
        AudioManager::get().update();      // update audio spatialization
        imgui_->endFrame();  // finalize draw data even if the frame is skipped (resize)

        renderer_->drawFrame(*scene_, camera_, project_.get());
    }
    vkDeviceWaitIdle(device_->device());
}

} // namespace ne
