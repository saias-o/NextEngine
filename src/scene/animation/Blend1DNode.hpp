#pragma once

// Blend1DNode — blend space 1D : chaque entrée est placée sur un seuil
// (ex. vitesse 0 = Idle, 1.5 = Walk, 4 = Run) et un paramètre du blackboard
// choisit la paire voisine à mélanger. C'est la recette locomotion du niveau
// d'authoring simple : aucun graphe visuel n'est nécessaire.

#include "scene/animation/AnimBlackboard.hpp"
#include "scene/animation/AnimNode.hpp"

#include <memory>
#include <vector>

namespace saida {

class Blend1DNode : public AnimNode {
public:
    // Les entrées sont maintenues triées par seuil.
    void addInput(float threshold, std::unique_ptr<AnimNode> node);

    // Le paramètre est lu à chaque update ; setValue() pilote sans blackboard.
    void bindParameter(const AnimBlackboard* blackboard, std::string_view paramName);
    void setValue(float value) { value_ = value; }
    float value() const { return value_; }

    void update(float deltaTime) override;
    void evaluate(const LocalPose& bindPose, LocalPose& outPose) const override;
    float normalizedTime() const override;
    void seekNormalized(float phase) override;

private:
    struct Input {
        float threshold = 0.0f;
        std::unique_ptr<AnimNode> node;
    };

    // Paire encadrant la valeur courante et poids de la seconde entrée.
    void activeSpan(size_t& lower, size_t& upper, float& weight) const;

    std::vector<Input> inputs_;
    const AnimBlackboard* blackboard_ = nullptr;
    uint32_t paramHash_ = 0;
    bool hasParam_ = false;
    float value_ = 0.0f;

    mutable LocalPose tempPoseLower_;
    mutable LocalPose tempPoseUpper_;
};

} // namespace saida
