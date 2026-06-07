#pragma once

#include "core/InputEnums.hpp"

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>

namespace ne {

class Window;

using InputContextID = std::string;
const InputContextID kGlobalContext = "Global";

struct ActionBinding {
    std::string actionName;
    InputContextID context = kGlobalContext;

    bool isKey = false;
    KeyCode key = KeyCode::Unknown;

    bool isMouse = false;
    MouseButton mouseBtn = MouseButton::Left;

    bool isGamepadBtn = false;
    GamepadButton padBtn = GamepadButton::A;

    bool isGamepadAxis = false;
    GamepadAxis padAxis = GamepadAxis::LeftX;
    float axisScale = 1.0f; // Multiplier (useful for inversion)
    float deadzone = 0.1f;

    // Runtime state (updated every frame)
    float currentValue = 0.0f;
    float previousValue = 0.0f;
};

using ActionCallback = std::function<void(float strength)>;

class Input {
public:
    // ---- Binding API ----
    static void bindKey(const std::string& action, KeyCode key, const InputContextID& context = kGlobalContext);
    static void bindMouse(const std::string& action, MouseButton btn, const InputContextID& context = kGlobalContext);
    static void bindGamepadAxis(const std::string& action, GamepadAxis axis, float scale = 1.0f, const InputContextID& context = kGlobalContext);
    
    static void unmapAction(const std::string& action, const InputContextID& context = kGlobalContext);
    static void clearAllActions();

    // ---- Query API (Gameplay) ----
    static bool isActionHeld(const std::string& action);
    static bool isActionJustPressed(const std::string& action);
    static bool isActionJustReleased(const std::string& action);
    static float getActionStrength(const std::string& action);

    static float getAxis(const std::string& negativeAction, const std::string& positiveAction);
    static glm::vec2 getVector(const std::string& left, const std::string& right, const std::string& down, const std::string& up);

    // ---- Raw Input Queries (Ignores Contexts & ImGui blocking) ----
    static bool isKeyDown(KeyCode key);
    static bool isKeyPressed(KeyCode key);
    static bool isMouseButtonDown(MouseButton btn);
    static bool isMouseButtonPressed(MouseButton btn);

    static glm::vec2 mouseDelta();          // movement since last frame
    static glm::vec2 mousePosition();       // cursor position in window pixels

    // ---- Event Consumption ----
    static void consumeMouse();
    static void consumeKeyboard();

    // ---- Context Management ----
    static void pushContext(const InputContextID& context);
    static void popContext(const InputContextID& context);

    // ---- Callback System ----
    static uint32_t subscribe(const std::string& action, InputEvent eventType, ActionCallback callback, const InputContextID& context = kGlobalContext);
    static void unsubscribe(uint32_t callbackId);

private:
    friend class Engine;
    static void bind(Window* window);  // called once at startup
    static void newFrame();            // sample state; called once per frame

    // Helper to evaluate binding value from raw input
    static float evaluateBinding(const ActionBinding& binding);
};

} // namespace ne
