#pragma once

#include "scene/Behaviour.hpp"

#include <glm/glm.hpp>

namespace ne {

// Spins its node continuously around a local axis — the demo behaviour driving
// the orbiting "moon" in the sample scenes. Serialized fields match the scene
// JSON: `axis` (rotation axis) and `speed` (degrees per second).
class RotatorBehaviour : public Behaviour {
public:
    void onUpdate(float dt) override;

    const char* typeName() const override { return "Rotator"; }
    void save(nlohmann::json& json) const override;
    void load(const nlohmann::json& json) override;

    glm::vec3 axis{0.0f, 1.0f, 0.0f};  // local rotation axis
    float speed = 70.0f;               // degrees per second
};

} // namespace ne
