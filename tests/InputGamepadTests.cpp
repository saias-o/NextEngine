#include "core/InputGamepad.hpp"

#include <cassert>
#include <cmath>

namespace {

bool near(float actual, float expected) {
    return std::abs(actual - expected) < 0.0001f;
}

void testSignedStickDeadzone() {
    using saida::GamepadAxis;
    using saida::input_detail::gamepadAxisActionValue;

    assert(near(gamepadAxisActionValue(GamepadAxis::LeftX, 0.09f, 1.0f, 0.1f), 0.0f));
    assert(near(gamepadAxisActionValue(GamepadAxis::LeftX, 0.55f, 1.0f, 0.1f), 0.5f));
    assert(near(gamepadAxisActionValue(GamepadAxis::LeftX, -0.55f, 1.0f, 0.1f), 0.0f));
    assert(near(gamepadAxisActionValue(GamepadAxis::LeftX, -0.55f, -1.0f, 0.1f), 0.5f));
    assert(near(gamepadAxisActionValue(GamepadAxis::LeftX, 1.0f, 2.0f, 0.1f), 1.0f));
}

void testTriggerNormalization() {
    using saida::GamepadAxis;
    using saida::input_detail::gamepadAxisActionValue;

    assert(near(gamepadAxisActionValue(GamepadAxis::RightTrigger, -1.0f, 1.0f, 0.1f), 0.0f));
    assert(near(gamepadAxisActionValue(GamepadAxis::RightTrigger, 0.0f, 1.0f, 0.1f),
                4.0f / 9.0f));
    assert(near(gamepadAxisActionValue(GamepadAxis::RightTrigger, 1.0f, 1.0f, 0.1f), 1.0f));
}

void testDeadzoneBounds() {
    using saida::input_detail::applyDeadzone;

    assert(near(applyDeadzone(0.5f, -1.0f), 0.5f));
    assert(near(applyDeadzone(0.98f, 2.0f), 0.0f));
    assert(near(applyDeadzone(1.0f, 2.0f), 1.0f));
}

} // namespace

int main() {
    testSignedStickDeadzone();
    testTriggerNormalization();
    testDeadzoneBounds();
    return 0;
}
