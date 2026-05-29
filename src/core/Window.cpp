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
}

Window::~Window() {
    glfwDestroyWindow(window_);
    glfwTerminate();
}

void Window::framebufferResizeCallback(GLFWwindow* window, int, int) {
    auto self = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
    self->resized_ = true;
}

VkSurfaceKHR Window::createSurface(VkInstance instance) const {
    VkSurfaceKHR surface;
    if (glfwCreateWindowSurface(instance, window_, nullptr, &surface) != VK_SUCCESS)
        throw std::runtime_error("failed to create window surface");
    return surface;
}

} // namespace ne
