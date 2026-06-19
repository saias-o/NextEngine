#include "core/Input.hpp"
#include "core/Window.hpp"

#include <algorithm>

namespace ne {

namespace {
Window* g_window = nullptr;

// Raw hardware state
bool g_keyCurr[GLFW_KEY_LAST + 1] = {};
bool g_keyPrev[GLFW_KEY_LAST + 1] = {};
bool g_mouseCurr[GLFW_MOUSE_BUTTON_LAST + 1] = {};
bool g_mousePrev[GLFW_MOUSE_BUTTON_LAST + 1] = {};
glm::vec2 g_mouseDelta{0.0f};
glm::vec2 g_mousePos{0.0f};
bool g_uiCapturesKeyboard = false;
bool g_uiCapturesMouse = false;

// Action bindings & state
std::vector<ActionBinding> g_bindings;
std::vector<InputContextID> g_contextStack = { kGlobalContext };

struct Subscription {
    uint32_t id;
    std::string actionName;
    InputEvent eventType;
    ActionCallback callback;
    InputContextID context;
};
std::vector<Subscription> g_subscriptions;
uint32_t g_nextSubscriptionId = 1;

// Mappings
int glfwKeyFromKeyCode(KeyCode key) {
    if (key == KeyCode::Unknown) return GLFW_KEY_UNKNOWN;
    if (key >= KeyCode::Space && key <= KeyCode::GraveAccent) {
        // ASCII based mappings
        switch (key) {
            case KeyCode::Space: return GLFW_KEY_SPACE;
            case KeyCode::Apostrophe: return GLFW_KEY_APOSTROPHE;
            case KeyCode::Comma: return GLFW_KEY_COMMA;
            case KeyCode::Minus: return GLFW_KEY_MINUS;
            case KeyCode::Period: return GLFW_KEY_PERIOD;
            case KeyCode::Slash: return GLFW_KEY_SLASH;
            case KeyCode::Num0: return GLFW_KEY_0;
            case KeyCode::Num1: return GLFW_KEY_1;
            case KeyCode::Num2: return GLFW_KEY_2;
            case KeyCode::Num3: return GLFW_KEY_3;
            case KeyCode::Num4: return GLFW_KEY_4;
            case KeyCode::Num5: return GLFW_KEY_5;
            case KeyCode::Num6: return GLFW_KEY_6;
            case KeyCode::Num7: return GLFW_KEY_7;
            case KeyCode::Num8: return GLFW_KEY_8;
            case KeyCode::Num9: return GLFW_KEY_9;
            case KeyCode::Semicolon: return GLFW_KEY_SEMICOLON;
            case KeyCode::Equal: return GLFW_KEY_EQUAL;
            case KeyCode::A: return GLFW_KEY_A;
            case KeyCode::B: return GLFW_KEY_B;
            case KeyCode::C: return GLFW_KEY_C;
            case KeyCode::D: return GLFW_KEY_D;
            case KeyCode::E: return GLFW_KEY_E;
            case KeyCode::F: return GLFW_KEY_F;
            case KeyCode::G: return GLFW_KEY_G;
            case KeyCode::H: return GLFW_KEY_H;
            case KeyCode::I: return GLFW_KEY_I;
            case KeyCode::J: return GLFW_KEY_J;
            case KeyCode::K: return GLFW_KEY_K;
            case KeyCode::L: return GLFW_KEY_L;
            case KeyCode::M: return GLFW_KEY_M;
            case KeyCode::N: return GLFW_KEY_N;
            case KeyCode::O: return GLFW_KEY_O;
            case KeyCode::P: return GLFW_KEY_P;
            case KeyCode::Q: return GLFW_KEY_Q;
            case KeyCode::R: return GLFW_KEY_R;
            case KeyCode::S: return GLFW_KEY_S;
            case KeyCode::T: return GLFW_KEY_T;
            case KeyCode::U: return GLFW_KEY_U;
            case KeyCode::V: return GLFW_KEY_V;
            case KeyCode::W: return GLFW_KEY_W;
            case KeyCode::X: return GLFW_KEY_X;
            case KeyCode::Y: return GLFW_KEY_Y;
            case KeyCode::Z: return GLFW_KEY_Z;
            case KeyCode::LeftBracket: return GLFW_KEY_LEFT_BRACKET;
            case KeyCode::Backslash: return GLFW_KEY_BACKSLASH;
            case KeyCode::RightBracket: return GLFW_KEY_RIGHT_BRACKET;
            case KeyCode::GraveAccent: return GLFW_KEY_GRAVE_ACCENT;
            default: return GLFW_KEY_UNKNOWN;
        }
    }
    
    switch (key) {
        case KeyCode::Escape: return GLFW_KEY_ESCAPE;
        case KeyCode::Enter: return GLFW_KEY_ENTER;
        case KeyCode::Tab: return GLFW_KEY_TAB;
        case KeyCode::Backspace: return GLFW_KEY_BACKSPACE;
        case KeyCode::Insert: return GLFW_KEY_INSERT;
        case KeyCode::Delete: return GLFW_KEY_DELETE;
        case KeyCode::Right: return GLFW_KEY_RIGHT;
        case KeyCode::Left: return GLFW_KEY_LEFT;
        case KeyCode::Down: return GLFW_KEY_DOWN;
        case KeyCode::Up: return GLFW_KEY_UP;
        case KeyCode::PageUp: return GLFW_KEY_PAGE_UP;
        case KeyCode::PageDown: return GLFW_KEY_PAGE_DOWN;
        case KeyCode::Home: return GLFW_KEY_HOME;
        case KeyCode::End: return GLFW_KEY_END;
        case KeyCode::LeftShift: return GLFW_KEY_LEFT_SHIFT;
        case KeyCode::LeftControl: return GLFW_KEY_LEFT_CONTROL;
        case KeyCode::LeftAlt: return GLFW_KEY_LEFT_ALT;
        case KeyCode::RightShift: return GLFW_KEY_RIGHT_SHIFT;
        case KeyCode::RightControl: return GLFW_KEY_RIGHT_CONTROL;
        case KeyCode::RightAlt: return GLFW_KEY_RIGHT_ALT;
        default: return GLFW_KEY_UNKNOWN;
    }
}

int glfwMouseFromMouseButton(MouseButton btn) {
    return static_cast<int>(btn);
}

} // namespace

void Input::bind(Window* window) {
    g_window = window;
    
    // Set up default bindings
    bindKey("MoveForward", KeyCode::W);
    bindKey("MoveBackward", KeyCode::S);
    bindKey("MoveLeft", KeyCode::A);
    bindKey("MoveRight", KeyCode::D);
    bindKey("MoveUp", KeyCode::E);
    bindKey("MoveDown", KeyCode::Q);
    
    bindKey("Jump", KeyCode::Space);
    bindKey("Sprint", KeyCode::LeftShift);
    
    bindMouse("Fire", MouseButton::Left);
    bindMouse("Aim", MouseButton::Right);
}

float Input::evaluateBinding(const ActionBinding& binding) {
    if (!g_window) return 0.0f;

    const bool ignoreUi = g_window->cursorCaptured();

    if (binding.isKey) {
        if (g_uiCapturesKeyboard && !ignoreUi) return 0.0f;
        int glfwKey = glfwKeyFromKeyCode(binding.key);
        if (glfwKey >= 0 && glfwKey <= GLFW_KEY_LAST) {
            return g_keyCurr[glfwKey] ? 1.0f : 0.0f;
        }
    } else if (binding.isMouse) {
        if (g_uiCapturesMouse && !ignoreUi) return 0.0f;
        int glfwBtn = glfwMouseFromMouseButton(binding.mouseBtn);
        if (glfwBtn >= 0 && glfwBtn <= GLFW_MOUSE_BUTTON_LAST) {
            return g_mouseCurr[glfwBtn] ? 1.0f : 0.0f;
        }
    }
    
    // Gamepad axis not implemented yet
    return 0.0f;
}

void Input::setUiCapture(bool keyboard, bool mouse) {
    g_uiCapturesKeyboard = keyboard;
    g_uiCapturesMouse = mouse;
}

void Input::newFrame() {
    if (!g_window) return;
    GLFWwindow* w = g_window->handle();

    // 1. Sample raw hardware state
    for (int k = 0; k <= GLFW_KEY_LAST; ++k) {
        g_keyPrev[k] = g_keyCurr[k];
        g_keyCurr[k] = glfwGetKey(w, k) == GLFW_PRESS;
    }
    for (int b = 0; b <= GLFW_MOUSE_BUTTON_LAST; ++b) {
        g_mousePrev[b] = g_mouseCurr[b];
        g_mouseCurr[b] = glfwGetMouseButton(w, b) == GLFW_PRESS;
    }

    double dx, dy;
    g_window->consumeMouseDelta(dx, dy);
    g_mouseDelta = {static_cast<float>(dx), static_cast<float>(dy)};

    double mx, my;
    glfwGetCursorPos(w, &mx, &my);
    g_mousePos = {static_cast<float>(mx), static_cast<float>(my)};

    // 2. Update bindings and trigger events
    for (auto& binding : g_bindings) {
        binding.previousValue = binding.currentValue;
        
        // Only evaluate if context matches one in the stack
        bool contextActive = false;
        for (const auto& ctx : g_contextStack) {
            if (ctx == binding.context) {
                contextActive = true;
                break;
            }
        }
        
        if (contextActive) {
            binding.currentValue = evaluateBinding(binding);
        } else {
            binding.currentValue = 0.0f;
        }
        
        // Dispatch callbacks
        for (const auto& sub : g_subscriptions) {
            if (sub.actionName == binding.actionName && sub.context == binding.context) {
                bool isPressed = binding.currentValue > 0.5f;
                bool wasPressed = binding.previousValue > 0.5f;
                
                if (sub.eventType == InputEvent::Pressed && isPressed && !wasPressed) {
                    sub.callback(binding.currentValue);
                } else if (sub.eventType == InputEvent::Held && isPressed) {
                    sub.callback(binding.currentValue);
                } else if (sub.eventType == InputEvent::Released && !isPressed && wasPressed) {
                    sub.callback(0.0f);
                }
            }
        }
    }
}

void Input::bindKey(const std::string& action, KeyCode key, const InputContextID& context) {
    ActionBinding b;
    b.actionName = action;
    b.context = context;
    b.isKey = true;
    b.key = key;
    g_bindings.push_back(b);
}

void Input::bindMouse(const std::string& action, MouseButton btn, const InputContextID& context) {
    ActionBinding b;
    b.actionName = action;
    b.context = context;
    b.isMouse = true;
    b.mouseBtn = btn;
    g_bindings.push_back(b);
}

void Input::bindGamepadAxis(const std::string& action, GamepadAxis axis, float scale, const InputContextID& context) {
    ActionBinding b;
    b.actionName = action;
    b.context = context;
    b.isGamepadAxis = true;
    b.padAxis = axis;
    b.axisScale = scale;
    g_bindings.push_back(b);
}

void Input::unmapAction(const std::string& action, const InputContextID& context) {
    g_bindings.erase(std::remove_if(g_bindings.begin(), g_bindings.end(), [&](const ActionBinding& b) {
        return b.actionName == action && b.context == context;
    }), g_bindings.end());
}

void Input::clearAllActions() {
    g_bindings.clear();
}

float Input::getActionStrength(const std::string& action) {
    float strength = 0.0f;
    for (const auto& b : g_bindings) {
        if (b.actionName == action) {
            // Check context stack
            for (auto it = g_contextStack.rbegin(); it != g_contextStack.rend(); ++it) {
                if (*it == b.context) {
                    strength = std::max(strength, b.currentValue);
                    break;
                }
            }
        }
    }
    return strength;
}

bool Input::isActionHeld(const std::string& action) {
    return getActionStrength(action) > 0.5f;
}

bool Input::isActionJustPressed(const std::string& action) {
    for (const auto& b : g_bindings) {
        if (b.actionName == action && b.currentValue > 0.5f && b.previousValue <= 0.5f) {
            // Check context stack
            for (auto it = g_contextStack.rbegin(); it != g_contextStack.rend(); ++it) {
                if (*it == b.context) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool Input::isActionJustReleased(const std::string& action) {
    for (const auto& b : g_bindings) {
        if (b.actionName == action && b.currentValue <= 0.5f && b.previousValue > 0.5f) {
            // Check context stack
            for (auto it = g_contextStack.rbegin(); it != g_contextStack.rend(); ++it) {
                if (*it == b.context) {
                    return true;
                }
            }
        }
    }
    return false;
}

float Input::getAxis(const std::string& negativeAction, const std::string& positiveAction) {
    return getActionStrength(positiveAction) - getActionStrength(negativeAction);
}

glm::vec2 Input::getVector(const std::string& left, const std::string& right, const std::string& down, const std::string& up) {
    return glm::vec2(getAxis(left, right), getAxis(down, up));
}

bool Input::isKeyDown(KeyCode key) {
    if (!g_window) return false;
    int glfwKey = glfwKeyFromKeyCode(key);
    if (glfwKey >= 0 && glfwKey <= GLFW_KEY_LAST) {
        return g_keyCurr[glfwKey];
    }
    return false;
}

bool Input::isKeyPressed(KeyCode key) {
    if (!g_window) return false;
    int glfwKey = glfwKeyFromKeyCode(key);
    if (glfwKey >= 0 && glfwKey <= GLFW_KEY_LAST) {
        return g_keyCurr[glfwKey] && !g_keyPrev[glfwKey];
    }
    return false;
}

bool Input::isMouseButtonDown(MouseButton btn) {
    if (!g_window) return false;
    int glfwBtn = glfwMouseFromMouseButton(btn);
    if (glfwBtn >= 0 && glfwBtn <= GLFW_MOUSE_BUTTON_LAST) {
        return g_mouseCurr[glfwBtn];
    }
    return false;
}

bool Input::isMouseButtonPressed(MouseButton btn) {
    if (!g_window) return false;
    int glfwBtn = glfwMouseFromMouseButton(btn);
    if (glfwBtn >= 0 && glfwBtn <= GLFW_MOUSE_BUTTON_LAST) {
        return g_mouseCurr[glfwBtn] && !g_mousePrev[glfwBtn];
    }
    return false;
}

glm::vec2 Input::mouseDelta() { return g_mouseDelta; }
glm::vec2 Input::mousePosition() { return g_mousePos; }

void Input::consumeMouse() {
    for (int b = 0; b <= GLFW_MOUSE_BUTTON_LAST; ++b) {
        g_mouseCurr[b] = false;
        g_mousePrev[b] = false;
    }
    g_mouseDelta = {0.0f, 0.0f};
    
    // Also clear bindings that are mouse-based
    for (auto& binding : g_bindings) {
        if (binding.isMouse) {
            binding.currentValue = 0.0f;
            binding.previousValue = 0.0f;
        }
    }
}

void Input::consumeKeyboard() {
    for (int k = 0; k <= GLFW_KEY_LAST; ++k) {
        g_keyCurr[k] = false;
        g_keyPrev[k] = false;
    }
    
    // Also clear bindings that are key-based
    for (auto& binding : g_bindings) {
        if (binding.isKey) {
            binding.currentValue = 0.0f;
            binding.previousValue = 0.0f;
        }
    }
}

void Input::pushContext(const InputContextID& context) {
    if (std::find(g_contextStack.begin(), g_contextStack.end(), context) == g_contextStack.end()) {
        g_contextStack.push_back(context);
    }
}

void Input::popContext(const InputContextID& context) {
    auto it = std::find(g_contextStack.begin(), g_contextStack.end(), context);
    if (it != g_contextStack.end()) {
        g_contextStack.erase(it);
    }
}

uint32_t Input::subscribe(const std::string& action, InputEvent eventType, ActionCallback callback, const InputContextID& context) {
    Subscription sub;
    sub.id = g_nextSubscriptionId++;
    sub.actionName = action;
    sub.eventType = eventType;
    sub.callback = std::move(callback);
    sub.context = context;
    g_subscriptions.push_back(sub);
    return sub.id;
}

void Input::unsubscribe(uint32_t callbackId) {
    g_subscriptions.erase(std::remove_if(g_subscriptions.begin(), g_subscriptions.end(), [&](const Subscription& s) {
        return s.id == callbackId;
    }), g_subscriptions.end());
}

} // namespace ne
