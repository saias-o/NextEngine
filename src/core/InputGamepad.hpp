#pragma once

#include "core/InputEnums.hpp"

#include <algorithm>
#include <cmath>

namespace saida::input_detail {

inline float applyDeadzone(float value, float deadzone) {
    value = std::clamp(value, -1.0f, 1.0f);
    deadzone = std::clamp(deadzone, 0.0f, 0.99f);
    const float magnitude = std::abs(value);
    if (magnitude <= deadzone) return 0.0f;
    return std::copysign((magnitude - deadzone) / (1.0f - deadzone), value);
}

inline bool isTriggerAxis(GamepadAxis axis) {
    return axis == GamepadAxis::LeftTrigger || axis == GamepadAxis::RightTrigger;
}

// GLFW reports sticks in [-1, 1] and triggers in [-1, 1], where -1 is
// released. Action bindings are directional strengths in [0, 1]; bind the
// opposite stick direction to another action with a negative scale.
inline float gamepadAxisActionValue(GamepadAxis axis, float rawValue, float scale,
                                    float deadzone) {
    float value = std::clamp(rawValue, -1.0f, 1.0f);
    if (isTriggerAxis(axis)) value = (value + 1.0f) * 0.5f;
    value = applyDeadzone(value, deadzone) * scale;
    return std::clamp(value, 0.0f, 1.0f);
}

} // namespace saida::input_detail
