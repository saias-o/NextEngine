#include "core/Input.hpp"

#include "core/Window.hpp"

namespace ne {

namespace {
Window* g_window = nullptr;

bool g_keyCurr[GLFW_KEY_LAST + 1] = {};
bool g_keyPrev[GLFW_KEY_LAST + 1] = {};
glm::vec2 g_mouseDelta{0.0f};
glm::vec2 g_mousePos{0.0f};

bool validKey(int key) { return key >= 0 && key <= GLFW_KEY_LAST; }
} // namespace

void Input::bind(Window* window) {
    g_window = window;
}

void Input::newFrame() {
    if (!g_window) return;
    GLFWwindow* w = g_window->handle();

    for (int k = 0; k <= GLFW_KEY_LAST; ++k) {
        g_keyPrev[k] = g_keyCurr[k];
        g_keyCurr[k] = glfwGetKey(w, k) == GLFW_PRESS;
    }

    // Single consume point for the accumulated mouse movement.
    double dx, dy;
    g_window->consumeMouseDelta(dx, dy);
    g_mouseDelta = {static_cast<float>(dx), static_cast<float>(dy)};

    double mx, my;
    glfwGetCursorPos(w, &mx, &my);
    g_mousePos = {static_cast<float>(mx), static_cast<float>(my)};
}

bool Input::keyDown(int glfwKey) {
    return validKey(glfwKey) && g_keyCurr[glfwKey];
}

bool Input::keyPressed(int glfwKey) {
    return validKey(glfwKey) && g_keyCurr[glfwKey] && !g_keyPrev[glfwKey];
}

bool Input::mouseButtonDown(int glfwButton) {
    return g_window && g_window->mouseButtonDown(glfwButton);
}

glm::vec2 Input::mouseDelta() { return g_mouseDelta; }
glm::vec2 Input::mousePosition() { return g_mousePos; }

} // namespace ne
