#include "scene/UICanvasNode.hpp"
#include "scene/UINode.hpp"
#include <nlohmann/json.hpp>

namespace saida {

UINode* UICanvasNode::raycast(float mouseX, float mouseY) {
    // Parcourt les enfants à l'envers (les derniers rendus sont au-dessus)
    for (auto it = children().rbegin(); it != children().rend(); ++it) {
        UINode* result = raycastRecursive(it->get(), mouseX, mouseY);
        if (result) return result;
    }
    return nullptr;
}

UINode* UICanvasNode::raycastRecursive(Node* node, float mouseX, float mouseY) {
    // On vérifie d'abord les enfants de ce noeud (ils sont affichés par-dessus lui)
    for (auto it = node->children().rbegin(); it != node->children().rend(); ++it) {
        UINode* result = raycastRecursive(it->get(), mouseX, mouseY);
        if (result) return result;
    }

    // Puis on vérifie ce noeud lui-même
    if (UINode* uiNode = dynamic_cast<UINode*>(node)) {
        if (uiNode->isPointInside(mouseX, mouseY)) {
            return uiNode;
        }
    }

    return nullptr;
}

void UICanvasNode::serialize(nlohmann::json& j, ResourceManager& resources) const {
    Node::serialize(j, resources);
    j["width"] = width_;
    j["height"] = height_;
}

void UICanvasNode::deserialize(const nlohmann::json& j, ResourceManager& resources) {
    Node::deserialize(j, resources);
    width_ = j.value("width", 1920.0f);
    height_ = j.value("height", 1080.0f);
}

} // namespace saida
