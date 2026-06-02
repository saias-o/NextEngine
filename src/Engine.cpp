#include "Engine.hpp"

#include <glm/gtc/matrix_transform.hpp>  // GLM_FORCE_* set globally by CMake
#include <glm/gtc/quaternion.hpp>

#include "core/Paths.hpp"
#include "core/Window.hpp"
#include "editor/EditorUI.hpp"
#include "graphics/ImGuiLayer.hpp"
#include "graphics/Material.hpp"
#include "graphics/Mesh.hpp"
#include "graphics/ResourceManager.hpp"
#include "graphics/Swapchain.hpp"
#include "graphics/Texture.hpp"
#include "graphics/VulkanDevice.hpp"
#include "project/Project.hpp"
#include "render/Renderer.hpp"
#include "imgui.h"
#include "scene/Behaviour.hpp"
#include "scene/LightNode.hpp"
#include "scene/MeshNode.hpp"
#include "scene/Scene.hpp"

#include <string>

namespace ne {

namespace {

constexpr uint32_t kWidth = 1600;
constexpr uint32_t kHeight = 900;

const std::string kTexturePath = assetPath("assets/textures/checker.png");

// A unit cube with per-face normals and UVs. Color is white so the fragment
// shader shows texture * lighting unmodified.
const std::vector<Vertex> kCubeVertices = {
    // +Z
    {{-0.5f, -0.5f,  0.5f}, {0, 0, 1}, {1, 1, 1}, {0, 1}}, {{ 0.5f, -0.5f,  0.5f}, {0, 0, 1}, {1, 1, 1}, {1, 1}},
    {{ 0.5f,  0.5f,  0.5f}, {0, 0, 1}, {1, 1, 1}, {1, 0}}, {{-0.5f,  0.5f,  0.5f}, {0, 0, 1}, {1, 1, 1}, {0, 0}},
    // -Z
    {{ 0.5f, -0.5f, -0.5f}, {0, 0, -1}, {1, 1, 1}, {0, 1}}, {{-0.5f, -0.5f, -0.5f}, {0, 0, -1}, {1, 1, 1}, {1, 1}},
    {{-0.5f,  0.5f, -0.5f}, {0, 0, -1}, {1, 1, 1}, {1, 0}}, {{ 0.5f,  0.5f, -0.5f}, {0, 0, -1}, {1, 1, 1}, {0, 0}},
    // +Y
    {{-0.5f,  0.5f,  0.5f}, {0, 1, 0}, {1, 1, 1}, {0, 1}}, {{ 0.5f,  0.5f,  0.5f}, {0, 1, 0}, {1, 1, 1}, {1, 1}},
    {{ 0.5f,  0.5f, -0.5f}, {0, 1, 0}, {1, 1, 1}, {1, 0}}, {{-0.5f,  0.5f, -0.5f}, {0, 1, 0}, {1, 1, 1}, {0, 0}},
    // -Y
    {{-0.5f, -0.5f, -0.5f}, {0, -1, 0}, {1, 1, 1}, {0, 1}}, {{ 0.5f, -0.5f, -0.5f}, {0, -1, 0}, {1, 1, 1}, {1, 1}},
    {{ 0.5f, -0.5f,  0.5f}, {0, -1, 0}, {1, 1, 1}, {1, 0}}, {{-0.5f, -0.5f,  0.5f}, {0, -1, 0}, {1, 1, 1}, {0, 0}},
    // +X
    {{ 0.5f, -0.5f,  0.5f}, {1, 0, 0}, {1, 1, 1}, {0, 1}}, {{ 0.5f, -0.5f, -0.5f}, {1, 0, 0}, {1, 1, 1}, {1, 1}},
    {{ 0.5f,  0.5f, -0.5f}, {1, 0, 0}, {1, 1, 1}, {1, 0}}, {{ 0.5f,  0.5f,  0.5f}, {1, 0, 0}, {1, 1, 1}, {0, 0}},
    // -X
    {{-0.5f, -0.5f, -0.5f}, {-1, 0, 0}, {1, 1, 1}, {0, 1}}, {{-0.5f, -0.5f,  0.5f}, {-1, 0, 0}, {1, 1, 1}, {1, 1}},
    {{-0.5f,  0.5f,  0.5f}, {-1, 0, 0}, {1, 1, 1}, {1, 0}}, {{-0.5f,  0.5f, -0.5f}, {-1, 0, 0}, {1, 1, 1}, {0, 0}},
};

const std::vector<uint32_t> kCubeIndices = {
     0,  1,  2,  2,  3,  0,   4,  5,  6,  6,  7,  4,
     8,  9, 10, 10, 11,  8,  12, 13, 14, 14, 15, 12,
    16, 17, 18, 18, 19, 16,  20, 21, 22, 22, 23, 20,
};

// Sample behaviour: continuously spins its node around an axis. Children inherit
// the rotation through the transform hierarchy.
class RotatorBehaviour : public Behaviour {
public:
    RotatorBehaviour(glm::vec3 axis, float degreesPerSecond)
        : axis_(glm::normalize(axis)), speed_(glm::radians(degreesPerSecond)) {}

    void onUpdate(float dt) override {
        angle_ += dt * speed_;
        node()->transform().rotation = glm::angleAxis(angle_, axis_);
    }

private:
    glm::vec3 axis_;
    float speed_;
    float angle_ = 0.0f;
};

} // namespace

Engine::Engine() {
    window_ = std::make_unique<Window>(kWidth, kHeight, "NextEngine");
    device_ = std::make_unique<VulkanDevice>(*window_);
    swapchain_ = std::make_unique<Swapchain>(*device_, *window_);
    resources_ = std::make_unique<ResourceManager>(*device_);

    buildScene();  // creates meshes/textures/materials via resources_

    imgui_ = std::make_unique<ImGuiLayer>(*device_, *window_, swapchain_->renderPass(),
        swapchain_->imageCount(), swapchain_->samples());
    renderer_ = std::make_unique<Renderer>(*device_, *swapchain_, *window_,
        resources_->materialSetLayout(), *imgui_);
    editorUI_ = std::make_unique<EditorUI>();
    project_ = std::make_unique<Project>();

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
        float dt = static_cast<float>(now - last);
        last = now;

        processInput(dt);
        scene_->updateTree(dt);  // runs node behaviours

        imgui_->beginFrame();
        editorUI_->draw(scene_.get(), &camera_, project_.get(), dt);
        imgui_->endFrame();  // finalize draw data even if the frame is skipped (resize)

        renderer_->drawFrame(*scene_, camera_);
    }
    vkDeviceWaitIdle(device_->device());
}

void Engine::buildScene() {
    scene_ = std::make_unique<Scene>();

    // Shared resources, loaded once and cached by the ResourceManager.
    // (To load the bugatti instead — untextured, no UVs — use:
    //  resources_->loadMesh(assetPath("models/bugatti/bugatti.obj")).)
    Mesh* cube = resources_->createMesh("cube", kCubeVertices, kCubeIndices);
    Texture* checker = resources_->loadTexture(kTexturePath);
    // Three materials sharing the texture but tinted differently (per-material data).
    Material* matWhite = resources_->createMaterial("white", checker, glm::vec4(1.0f));
    Material* matWarm = resources_->createMaterial("warm", checker, glm::vec4(1.0f, 0.55f, 0.4f, 1.0f));
    Material* matCool = resources_->createMaterial("cool", checker, glm::vec4(0.45f, 0.7f, 1.0f, 1.0f));

    // A small hierarchy to show transform inheritance: a central "planet" with
    // an orbiting "moon", which itself has an orbiting "sub-moon". They share the
    // cube mesh but each has its own material. The orbiting motion comes entirely
    // from RotatorBehaviours on the parents — children inherit it.
    Node* planet = scene_->createChild<MeshNode>("planet", cube, matWhite);
    planet->addBehaviour<RotatorBehaviour>(glm::vec3(0, 1, 0), 40.0f);

    Node* moon = planet->createChild<MeshNode>("moon", cube, matWarm);
    moon->transform().position = {2.0f, 0.0f, 0.0f};
    moon->transform().scale = glm::vec3(0.45f);
    moon->addBehaviour<RotatorBehaviour>(glm::vec3(0, 1, 0), 90.0f);

    Node* subMoon = moon->createChild<MeshNode>("sub-moon", cube, matCool);
    subMoon->transform().position = {2.0f, 0.0f, 0.0f};
    subMoon->transform().scale = glm::vec3(0.5f);

    // A directional "sun" (warm, slightly overhead).
    sun_ = scene_->createChild<LightNode>("sun", LightType::Directional);
    sun_->direction = glm::normalize(glm::vec3(-0.5f, -1.0f, -0.3f));
    sun_->color = {1.0f, 0.95f, 0.85f};
    sun_->intensity = 1.0f;

    // A point light orbiting the scene: a rotating pivot with the light offset
    // from it, so the RotatorBehaviour makes it circle the objects.
    Node* lightPivot = scene_->createChild<Node>("light-pivot");
    lightPivot->addBehaviour<RotatorBehaviour>(glm::vec3(0, 1, 0), 70.0f);
    lamp_ = lightPivot->createChild<LightNode>("lamp", LightType::Point);
    lamp_->transform().position = {3.5f, 1.5f, 0.0f};
    lamp_->color = {0.3f, 0.5f, 1.0f};
    lamp_->intensity = 4.0f;
    lamp_->range = 8.0f;
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
        // In play mode, TAB toggles between captured/free
        bool tabDown = window_->keyDown(GLFW_KEY_TAB);
        if (tabDown && !tabWasDown_)
            window_->setCursorCaptured(!window_->cursorCaptured());
        tabWasDown_ = tabDown;
    } else {
        // In scene mode, fly only when holding right-click over the viewport
        bool rightClick = window_->mouseButtonDown(GLFW_MOUSE_BUTTON_RIGHT);
        if (rightClick && !window_->cursorCaptured()) {
            if (!ImGui::GetIO().WantCaptureMouse) {
                window_->setCursorCaptured(true);
            }
        } else if (!rightClick && window_->cursorCaptured()) {
            window_->setCursorCaptured(false);
        }
    }
}

void Engine::processInput(float dt) {
    bool escDown = window_->keyDown(GLFW_KEY_ESCAPE);
    if (escDown && !escWasDown_) {
        if (editorUI_ && editorUI_->isPlayMode()) {
            editorUI_->setPlayMode(false); // Exits Play Mode back to Scene Mode
        } else {
            window_->close();
            return;
        }
    }
    escWasDown_ = escDown;

    if (editorUI_ && editorUI_->quitRequested()) {
        window_->close();
        return;
    }

    updateCursorCapture(editorUI_->isPlayMode());

    // Always drain the accumulated mouse delta so it can't pile up while in UI.
    double dx, dy;
    window_->consumeMouseDelta(dx, dy);

    // When the cursor is free (UI mode), ImGui owns the mouse; don't fly.
    if (!window_->cursorCaptured())
        return;

    // Mouse look.
    constexpr float sensitivity = 0.1f;
    camera_.rotate(static_cast<float>(dx) * sensitivity,
                   -static_cast<float>(dy) * sensitivity);  // screen Y is down

    // Keyboard movement (WASD horizontal, Space/Ctrl up/down). Shift to sprint.
    float speed = (window_->keyDown(GLFW_KEY_LEFT_SHIFT) ? 6.0f : 2.5f) * dt;
    glm::vec3 front = camera_.front();
    glm::vec3 right = camera_.right();
    if (window_->keyDown(GLFW_KEY_W)) camera_.position += front * speed;
    if (window_->keyDown(GLFW_KEY_S)) camera_.position -= front * speed;
    if (window_->keyDown(GLFW_KEY_D)) camera_.position += right * speed;
    if (window_->keyDown(GLFW_KEY_A)) camera_.position -= right * speed;
    if (window_->keyDown(GLFW_KEY_SPACE)) camera_.position += glm::vec3(0, 1, 0) * speed;
    if (window_->keyDown(GLFW_KEY_LEFT_CONTROL)) camera_.position -= glm::vec3(0, 1, 0) * speed;
}

} // namespace ne
