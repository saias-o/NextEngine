#pragma once

#include <glm/glm.hpp>

namespace ne {

class Scene;
class UINode;
class UIInteractableNode;

class UIInteractionSystem {
public:
    UIInteractionSystem() = default;
    ~UIInteractionSystem() = default;

    // Returns true if a UI node handled the event (hovered or pressed)
    bool update(Scene& scene, const glm::vec2& mousePos, bool isLeftDown, bool isLeftJustPressed, bool isLeftJustReleased);

private:
    UIInteractableNode* raycast(UINode* node, const glm::vec2& mousePos);

    UIInteractableNode* hoveredNode_ = nullptr;
    UIInteractableNode* pressedNode_ = nullptr;
};

} // namespace ne
