#pragma once

#include "scene/Node.hpp"

#include <glm/glm.hpp>

#include <string>
#include <utility>

namespace ne {

enum class LightType {
    Directional,  // infinitely far; uses `direction`
    Point,        // local; uses the node's world position and `range`
};

// How a light is meant to be evaluated. Everything is realtime today, but the
// field lets a future bake step know which lights to precompute into lightmaps.
enum class LightBakeMode {
    Realtime,  // always computed live
    Baked,     // contribution precomputed offline (static geometry only)
    Mixed,     // direct realtime + baked indirect
};

// A light source in the scene graph (cf. Godot Light3D / Unity Light).
class LightNode : public Node {
public:
    explicit LightNode(std::string name, LightType type = LightType::Directional)
        : Node(std::move(name)), type(type) {}

    LightNode* asLight() override { return this; }

    LightType type;
    glm::vec3 color{1.0f};
    float intensity = 1.0f;

    // Directional only: light travel direction in the node's local space
    // (rotated by the node's world transform). Point lights ignore this and use
    // the node's world position instead.
    glm::vec3 direction{0.0f, -1.0f, 0.0f};
    float range = 10.0f;  // point lights only

    LightBakeMode bakeMode = LightBakeMode::Realtime;
};

} // namespace ne
