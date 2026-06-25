#include "ui/UIInteractionSystem.hpp"
#include "scene/Scene.hpp"
#include "scene/UICanvasNode.hpp"
#include "scene/UIInteractableNode.hpp"
#include "scene/WebCanvasNode.hpp"

namespace ne {

bool UIInteractionSystem::update(Scene& scene, const glm::vec2& mousePos, bool isLeftDown, bool isLeftJustPressed, bool isLeftJustReleased) {
    UICanvasNode* canvas = scene.uiCanvas();
    if (!canvas || !canvas->isActiveInHierarchy()) {
        if (hoveredNode_) {
            hoveredNode_->onHoverExit();
            hoveredNode_ = nullptr;
        }
        return false;
    }
    
    // We pass the screen size to updateTransforms for anchors
    // Note: The UI Canvas size is currently assumed to be the screen size.
    // In a real engine, we'd get the actual screen size from the renderer/window,
    // but for now 1600x900 or whatever the UIRenderer uses works if we assume 1:1.
    for (auto& child : canvas->children()) {
        if (auto* uiChild = dynamic_cast<UINode*>(child.get())) {
            uiChild->updateTransforms(0.0f, 0.0f, 1600.0f, 900.0f);
        }
    }

    // 2. Raycast
    UIInteractableNode* targetNode = nullptr;
    // On itère à l'envers (les éléments dessinés en dernier sont au-dessus)
    const auto& children = canvas->children();
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        if (UINode* uiChild = dynamic_cast<UINode*>(it->get())) {
            targetNode = raycast(uiChild, mousePos);
            if (targetNode) break;
        }
    }

    // 3. Machine à états
    // Hover
    if (targetNode != hoveredNode_) {
        if (hoveredNode_) hoveredNode_->onHoverExit();
        hoveredNode_ = targetNode;
        if (hoveredNode_) hoveredNode_->onHoverEnter();
    }

    // Press
    if (isLeftJustPressed) {
        if (hoveredNode_) {
            hoveredNode_->onMouseDown();
            pressedNode_ = hoveredNode_;
        } else {
            pressedNode_ = nullptr;
        }
    }

    // Click / Release
    if (isLeftJustReleased) {
        if (pressedNode_) {
            if (pressedNode_ == hoveredNode_) {
                pressedNode_->onClick();
            }
            pressedNode_->onMouseUp();
            pressedNode_ = nullptr;
        }
    }
    
    // Fallback if mouse leaves window while pressed (not always perfect depending on OS, but safe)
    if (!isLeftDown && pressedNode_) {
        pressedNode_->onMouseUp();
        pressedNode_ = nullptr;
    }
    
    // 4. WebCanvasNode Updates & Inputs
    for (auto* wcn : scene.webCanvases()) {
        if (!wcn->isActiveInHierarchy()) continue;
        
        if (wcn->mode() == WebCanvasNode::Mode::ScreenSpace) {
            // Check if mouse is inside the ScreenSpace WebCanvas (which takes the whole screen up to its width/height)
            if (mousePos.x >= 0 && mousePos.x <= wcn->width() &&
                mousePos.y >= 0 && mousePos.y <= wcn->height()) {
                
                int mx = static_cast<int>(mousePos.x);
                int my = static_cast<int>(mousePos.y);
                
                if (isLeftJustPressed) {
                    wcn->fireMouseEvent(WebCanvasNode::MouseEvent::Down, mx, my, WebCanvasNode::MouseButton::Left);
                } else if (isLeftJustReleased) {
                    wcn->fireMouseEvent(WebCanvasNode::MouseEvent::Up, mx, my, WebCanvasNode::MouseButton::Left);
                } else {
                    wcn->fireMouseEvent(WebCanvasNode::MouseEvent::Move, mx, my,
                                        isLeftDown ? WebCanvasNode::MouseButton::Left : WebCanvasNode::MouseButton::None);
                }
            }
        }
    }
    
    return targetNode != nullptr || hoveredNode_ != nullptr || pressedNode_ != nullptr;
}

UIInteractableNode* UIInteractionSystem::raycast(UINode* node, const glm::vec2& mousePos) {
    if (!node->isActiveInHierarchy()) return nullptr;

    float globalX = node->globalX();
    float globalY = node->globalY();

    // Reverse DFS sur les enfants d'abord pour attraper les enfants au-dessus
    const auto& children = node->children();
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        if (UINode* uiChild = dynamic_cast<UINode*>(it->get())) {
            UIInteractableNode* hit = raycast(uiChild, mousePos);
            if (hit) return hit;
        }
    }

    // Si aucun enfant n'est touché, tester ce noeud (si interactable)
    if (auto interactable = dynamic_cast<UIInteractableNode*>(node)) {
        if (!interactable->interactable()) return nullptr;

        float drawX = globalX - (node->width() * node->pivotX());
        float drawY = globalY - (node->height() * node->pivotY());

        if (mousePos.x >= drawX && mousePos.x <= drawX + node->width() &&
            mousePos.y >= drawY && mousePos.y <= drawY + node->height()) {
            return interactable;
        }
    }

    return nullptr;
}

} // namespace ne
