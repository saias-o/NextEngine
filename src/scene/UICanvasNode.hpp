#pragma once

#include "scene/Node.hpp"

namespace saida {

class UINode;

class UICanvasNode : public Node {
public:
    UICanvasNode() = default;
    virtual ~UICanvasNode() = default;

    // Dimensions du canvas (souvent la taille de l'écran/fenêtre)
    float width() const { return width_; }
    float height() const { return height_; }
    void setSize(float w, float h) { width_ = w; height_ = h; }

    // Interroge l'arbre pour trouver le noeud d'UI cliqué
    UINode* raycast(float mouseX, float mouseY);

    const char* typeName() const override { return "UICanvasNode"; }
    void serialize(nlohmann::json& j, ResourceManager& resources) const override;
    void deserialize(const nlohmann::json& j, ResourceManager& resources) override;

private:
    UINode* raycastRecursive(Node* node, float mouseX, float mouseY);

    float width_ = 1920.0f;
    float height_ = 1080.0f;
};

} // namespace saida
