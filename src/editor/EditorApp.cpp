#include "editor/EditorApp.hpp"

#include "Engine.hpp"
#include "core/Camera.hpp"
#include "core/Input.hpp"
#include "core/Window.hpp"

#include "imgui.h"

#include <glm/glm.hpp>

namespace ne {

EditorApp::EditorApp(Engine& engine) : engine_(engine) {}

void EditorApp::update(float dt) {
    processInput(dt);
    ui_.draw(&engine_.scene(), &engine_.camera(), &engine_.project(), &engine_.resources(), dt);
}

void EditorApp::updateCursorCapture(bool isPlayMode) {
    Window& window = engine_.window();

    if (isPlayMode && !wasPlayMode_) {
        window.setCursorCaptured(true);   // capture on entering play mode
    } else if (!isPlayMode && wasPlayMode_) {
        window.setCursorCaptured(false);  // release on leaving play mode
    }
    wasPlayMode_ = isPlayMode;

    if (isPlayMode) {
        if (Input::keyPressed(GLFW_KEY_TAB))
            window.setCursorCaptured(!window.cursorCaptured());
    } else {
        // Scene mode: fly only while holding right-click over the viewport.
        bool rightClick = Input::mouseButtonDown(GLFW_MOUSE_BUTTON_RIGHT);
        if (rightClick && !window.cursorCaptured()) {
            if (!ImGui::GetIO().WantCaptureMouse)
                window.setCursorCaptured(true);
        } else if (!rightClick && window.cursorCaptured()) {
            window.setCursorCaptured(false);
        }
    }
}

void EditorApp::processInput(float dt) {
    Window& window = engine_.window();
    Camera& camera = engine_.camera();

    if (Input::keyPressed(GLFW_KEY_ESCAPE)) {
        if (ui_.isPlayMode()) {
            ui_.setPlayMode(false);  // exit play mode back to scene mode
        } else {
            window.close();
            return;
        }
    }

    if (ui_.quitRequested()) {
        window.close();
        return;
    }

    updateCursorCapture(ui_.isPlayMode());

    // Cursor free (UI mode): ImGui owns the mouse. But over the 3D viewport,
    // allow pan (middle-drag) and zoom (wheel).
    if (!window.cursorCaptured()) {
        ImGuiIO& io = ImGui::GetIO();
        bool inViewport = ui_.isViewportHovered(Input::mousePosition().x, Input::mousePosition().y);
        if (inViewport || !io.WantCaptureMouse) {
            if (Input::mouseButtonDown(GLFW_MOUSE_BUTTON_MIDDLE)) {
                glm::vec2 d = Input::mouseDelta();
                constexpr float panSpeed = 0.01f;
                camera.position -= camera.right() * d.x * panSpeed;
                camera.position += camera.up() * d.y * panSpeed;  // screen Y is down
            }
            if (io.MouseWheel != 0.0f)
                camera.position += camera.front() * io.MouseWheel * 2.0f;
        }
        return;
    }

    // Captured: FPS look + WASD movement.
    constexpr float sensitivity = 0.1f;
    glm::vec2 d = Input::mouseDelta();
    camera.rotate(d.x * sensitivity, -d.y * sensitivity);  // screen Y is down

    float speed = (Input::keyDown(GLFW_KEY_LEFT_SHIFT) ? 6.0f : 2.5f) * dt;
    glm::vec3 front = camera.front();
    glm::vec3 right = camera.right();
    if (Input::keyDown(GLFW_KEY_W)) camera.position += front * speed;
    if (Input::keyDown(GLFW_KEY_S)) camera.position -= front * speed;
    if (Input::keyDown(GLFW_KEY_D)) camera.position += right * speed;
    if (Input::keyDown(GLFW_KEY_A)) camera.position -= right * speed;
    if (Input::keyDown(GLFW_KEY_SPACE)) camera.position += glm::vec3(0, 1, 0) * speed;
    if (Input::keyDown(GLFW_KEY_LEFT_CONTROL)) camera.position -= glm::vec3(0, 1, 0) * speed;
}

} // namespace ne
