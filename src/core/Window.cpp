#include "core/Window.hpp"

#include <stdexcept>
#include <utility>

namespace ne {

Window::Window(int width, int height, std::string title) {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    window_ = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    glfwSetWindowUserPointer(window_, this);
    glfwSetFramebufferSizeCallback(window_, framebufferResizeCallback);

    // FPS-style mouse look: capture and hide the cursor, enable raw motion.
    glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    if (glfwRawMouseMotionSupported())
        glfwSetInputMode(window_, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    glfwSetCursorPosCallback(window_, cursorPosCallback);
}

Window::~Window() {
    glfwDestroyWindow(window_);
    glfwTerminate();
}

void Window::framebufferResizeCallback(GLFWwindow* window, int, int) {
    auto self = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
    self->resized_ = true;
}

void Window::cursorPosCallback(GLFWwindow* window, double x, double y) {
    auto self = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self->firstMouse_) {
        self->lastX_ = x;
        self->lastY_ = y;
        self->firstMouse_ = false;
    }
    self->mouseDx_ += x - self->lastX_;
    self->mouseDy_ += y - self->lastY_;
    self->lastX_ = x;
    self->lastY_ = y;
}

void Window::consumeMouseDelta(double& dx, double& dy) {
    dx = mouseDx_;
    dy = mouseDy_;
    mouseDx_ = 0.0;
    mouseDy_ = 0.0;
}

VkSurfaceKHR Window::createSurface(VkInstance instance) const {
    VkSurfaceKHR surface;
    if (glfwCreateWindowSurface(instance, window_, nullptr, &surface) != VK_SUCCESS)
        throw std::runtime_error("failed to create window surface");
    return surface;
}

} // namespace ne
