#pragma once

#include <glm/glm.hpp>

namespace ne {

class Window;

// Global input access, à la Unity's `Input`. Sampled once per frame by the
// Engine from the bound Window, so every reader (editor, behaviours) sees a
// consistent snapshot and the mouse delta has a single consume point.
//
// Key/button codes are GLFW codes (e.g. GLFW_KEY_W, GLFW_MOUSE_BUTTON_RIGHT);
// include <GLFW/glfw3.h> (or core/Window.hpp) to name them. Single-window,
// not thread-safe — fine for this engine.
class Input {
public:
    static bool keyDown(int glfwKey);      // held this frame
    static bool keyPressed(int glfwKey);   // went down this frame (rising edge)
    static bool mouseButtonDown(int glfwButton);

    static glm::vec2 mouseDelta();          // movement since last frame
    static glm::vec2 mousePosition();       // cursor position in window pixels

private:
    friend class Engine;
    static void bind(Window* window);  // called once at startup
    static void newFrame();            // sample state; called once per frame
};

} // namespace ne
