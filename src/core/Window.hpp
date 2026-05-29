#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <string>

namespace ne {

// Owns the GLFW window and reports framebuffer resize events.
class Window {
public:
    Window(int width, int height, std::string title);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool shouldClose() const { return glfwWindowShouldClose(window_); }
    void pollEvents() const { glfwPollEvents(); }
    void waitEvents() const { glfwWaitEvents(); }

    VkSurfaceKHR createSurface(VkInstance instance) const;
    void framebufferSize(int& width, int& height) const { glfwGetFramebufferSize(window_, &width, &height); }

    bool wasResized() const { return resized_; }
    void resetResizedFlag() { resized_ = false; }

    GLFWwindow* handle() const { return window_; }

private:
    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);

    GLFWwindow* window_ = nullptr;
    bool resized_ = false;
};

} // namespace ne
