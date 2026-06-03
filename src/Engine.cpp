#include "Engine.hpp"

#include <glm/glm.hpp>  // GLM_FORCE_* set globally by CMake

#include "core/Input.hpp"
#include "core/Time.hpp"
#include "core/Window.hpp"
#include "editor/EditorUI.hpp"
#include "graphics/ImGuiLayer.hpp"
#include "graphics/ResourceManager.hpp"
#include "graphics/Swapchain.hpp"
#include "graphics/VulkanDevice.hpp"
#include "project/Project.hpp"
#include "render/Renderer.hpp"
#include "imgui.h"
#include "scene/BehaviourRegistry.hpp"
#include "scene/Scene.hpp"
#include "scene/SceneSettings.hpp"
#include "scene/SceneSerializer.hpp"
#include "core/Log.hpp"

namespace ne {

namespace {

constexpr uint32_t kWidth = 1600;
constexpr uint32_t kHeight = 900;

} // namespace

Engine::Engine(SceneSetup sceneSetup, const std::string& initialProject) {
    // Engine built-in behaviours, registered so scenes can deserialize them.
    BehaviourRegistry::instance().registerType<SceneSettingsBehaviour>("SceneSettings");

    window_ = std::make_unique<Window>(kWidth, kHeight, "NextEngine");
    Input::bind(window_.get());
    device_ = std::make_unique<VulkanDevice>(*window_);
    swapchain_ = std::make_unique<Swapchain>(*device_, *window_);
    resources_ = std::make_unique<ResourceManager>(*device_);

    scene_ = std::make_unique<Scene>();
    project_ = std::make_unique<Project>();

    if (!initialProject.empty()) {
        if (!project_->load(initialProject)) {
            Log::warn("Failed to load initial project: " + initialProject);
        }
    }

    if (!project_->isLoaded()) {
        if (sceneSetup)
            sceneSetup(*scene_, *resources_);
    }

    imgui_ = std::make_unique<ImGuiLayer>(*device_, *window_, swapchain_->renderPass(),
        swapchain_->imageCount(), swapchain_->samples());
    renderer_ = std::make_unique<Renderer>(*device_, *swapchain_, *window_,
        resources_->materialSetLayout(), *imgui_);
    editorUI_ = std::make_unique<EditorUI>();

    // Pull back and look slightly down to see the whole orbiting hierarchy.
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

        processInput(realDt);              // editor camera: unscaled real time
        scene_->updateTree(Time::delta()); // behaviours: scaled time (pausable)

        editorUI_->draw(scene_.get(), &camera_, project_.get(), resources_.get(), realDt);
        imgui_->endFrame();  // finalize draw data even if the frame is skipped (resize)

        renderer_->drawFrame(*scene_, camera_);
    }
    vkDeviceWaitIdle(device_->device());
}

// drawUI() has been replaced by EditorUI::draw() — see editor/EditorUI.cpp.

void Engine::updateCursorCapture(bool isPlayMode) {
    if (isPlayMode && !wasPlayMode_) {
        // Auto-capture cursor when entering play mode
        window_->setCursorCaptured(true);
    } else if (!isPlayMode && wasPlayMode_) {
        // Auto-release cursor when leaving play mode
        window_->setCursorCaptured(false);
    }
    wasPlayMode_ = isPlayMode;

    if (isPlayMode) {
        // In play mode, TAB toggles between captured/free.
        if (Input::keyPressed(GLFW_KEY_TAB))
            window_->setCursorCaptured(!window_->cursorCaptured());
    } else {
        // In scene mode, fly only while holding right-click over the viewport.
        bool rightClick = Input::mouseButtonDown(GLFW_MOUSE_BUTTON_RIGHT);
        if (rightClick && !window_->cursorCaptured()) {
            if (!ImGui::GetIO().WantCaptureMouse)
                window_->setCursorCaptured(true);
        } else if (!rightClick && window_->cursorCaptured()) {
            window_->setCursorCaptured(false);
        }
    }
}

void Engine::processInput(float dt) {
    if (Input::keyPressed(GLFW_KEY_ESCAPE)) {
        if (editorUI_ && editorUI_->isPlayMode()) {
            editorUI_->setPlayMode(false);  // exit Play Mode back to Scene Mode
        } else {
            window_->close();
            return;
        }
    }

    if (editorUI_ && editorUI_->quitRequested()) {
        window_->close();
        return;
    }

    updateCursorCapture(editorUI_->isPlayMode());

    // When the cursor is free (UI mode), ImGui owns the mouse; don't fly.
    // However, if the user interacts with the 3D viewport (central node, WantCaptureMouse=false),
    // allow panning (Middle Mouse Drag) and zooming (Mouse Wheel).
    if (!window_->cursorCaptured()) {
        ImGuiIO& io = ImGui::GetIO();
        bool inViewport = editorUI_ && editorUI_->isViewportHovered(Input::mousePosition().x, Input::mousePosition().y);
        
        // Allow viewport navigation if explicitly hovering the 3D viewport,
        // or if ImGui isn't capturing the mouse at all.
        if (inViewport || !io.WantCaptureMouse) {
            // Panning: Middle Mouse Drag
            if (Input::mouseButtonDown(GLFW_MOUSE_BUTTON_MIDDLE)) {
                glm::vec2 d = Input::mouseDelta();
                float panSpeed = 0.01f;
                // Move camera opposite to mouse movement to drag the scene
                camera_.position -= camera_.right() * d.x * panSpeed;
                camera_.position += camera_.up() * d.y * panSpeed; // Screen Y is down
            }
            // Zooming: Mouse Wheel
            if (io.MouseWheel != 0.0f) {
                float zoomSpeed = 2.0f; // Increased for better feel
                camera_.position += camera_.front() * io.MouseWheel * zoomSpeed;
            }
        }
        return;
    }

    // Mouse look.
    constexpr float sensitivity = 0.1f;
    glm::vec2 d = Input::mouseDelta();
    camera_.rotate(d.x * sensitivity, -d.y * sensitivity);  // screen Y is down

    // Keyboard movement (WASD horizontal, Space/Ctrl up/down). Shift to sprint.
    float speed = (Input::keyDown(GLFW_KEY_LEFT_SHIFT) ? 6.0f : 2.5f) * dt;
    glm::vec3 front = camera_.front();
    glm::vec3 right = camera_.right();
    if (Input::keyDown(GLFW_KEY_W)) camera_.position += front * speed;
    if (Input::keyDown(GLFW_KEY_S)) camera_.position -= front * speed;
    if (Input::keyDown(GLFW_KEY_D)) camera_.position += right * speed;
    if (Input::keyDown(GLFW_KEY_A)) camera_.position -= right * speed;
    if (Input::keyDown(GLFW_KEY_SPACE)) camera_.position += glm::vec3(0, 1, 0) * speed;
    if (Input::keyDown(GLFW_KEY_LEFT_CONTROL)) camera_.position -= glm::vec3(0, 1, 0) * speed;
}

} // namespace ne
