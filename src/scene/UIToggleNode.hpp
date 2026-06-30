#pragma once

#include "scene/UIInteractableNode.hpp"
#include <glm/glm.hpp>

namespace saida {

class UIToggleNode : public UIInteractableNode {
public:
    UIToggleNode() = default;
    virtual ~UIToggleNode() = default;

    bool isOn() const { return isOn_; }
    void setIsOn(bool on) { isOn_ = on; }

    glm::vec4 onColor() const { return onColor_; }
    glm::vec4 offColor() const { return offColor_; }
    
    void setColors(const glm::vec4& onC, const glm::vec4& offC) {
        onColor_ = onC;
        offColor_ = offC;
    }

    void onClick() override {
        if (interactable_) {
            isOn_ = !isOn_;
        }
    }

    // Helper pour récupérer la couleur actuelle
    glm::vec4 currentColor() const {
        glm::vec4 baseColor = isOn_ ? onColor_ : offColor_;
        if (!interactable_) return baseColor * 0.5f;
        
        switch (state_) {
            case State::Hover: return baseColor * 1.1f; // Éclaircit au survol
            case State::Pressed: return baseColor * 0.8f; // Assombrit au clic
            default: return baseColor;
        }
    }

    const char* typeName() const override { return "UIToggleNode"; }
    void serialize(nlohmann::json& j, ResourceManager& resources) const override;
    void deserialize(const nlohmann::json& j, ResourceManager& resources) override;

private:
    bool isOn_ = false;
    glm::vec4 onColor_ = glm::vec4(0.2f, 0.8f, 0.2f, 1.0f); // Vert par défaut
    glm::vec4 offColor_ = glm::vec4(0.8f, 0.2f, 0.2f, 1.0f); // Rouge par défaut
};

} // namespace saida
