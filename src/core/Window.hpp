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

    void close() { glfwSetWindowShouldClose(window_, GLFW_TRUE); }

    // --- input ---
    bool keyDown(int glfwKey) const { return glfwGetKey(window_, glfwKey) == GLFW_PRESS; }
    // Mouse movement accumulated since the last call (then reset to zero).
    void consumeMouseDelta(double& dx, double& dy);

    GLFWwindow* handle() const { return window_; }

private:
    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
    static void cursorPosCallback(GLFWwindow* window, double x, double y);

    GLFWwindow* window_ = nullptr;
    bool resized_ = false;

    double mouseDx_ = 0.0;
    double mouseDy_ = 0.0;
    double lastX_ = 0.0;
    double lastY_ = 0.0;
    bool firstMouse_ = true;
};

} // namespace ne
