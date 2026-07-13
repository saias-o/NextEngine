#pragma once

// LayerNode — superpose une couche (ex. haut du corps) sur une pose de base,
// restreinte par un BoneMask et pondérée. Deux modes : Override (mélange vers
// la pose de la couche) et Additive (ajoute le delta couche − pose de repos).

#include "scene/animation/AnimNode.hpp"
#include "scene/animation/Rig.hpp"

#include <memory>
#include <string>
#include <vector>

namespace saida {

// Poids par os [0,1]. Un masque vide vaut 1 partout.
struct BoneMask {
    std::vector<float> weights;

    float weightFor(size_t boneIndex) const {
        return boneIndex < weights.size() ? weights[boneIndex] : 1.0f;
    }

    // Masque couvrant `rootBone` et toute sa descendance (chaîne "haut du
    // corps" typique : fromChain(rig, "Spine")).
    static BoneMask fromChain(const Rig& rig, const std::string& rootBone,
                              float weight = 1.0f);
};

class LayerNode : public AnimNode {
public:
    enum class Mode { Override, Additive };

    void setBase(std::unique_ptr<AnimNode> node) { base_ = std::move(node); }
    void setOverlay(std::unique_ptr<AnimNode> node) { overlay_ = std::move(node); }
    void setMask(BoneMask mask) { mask_ = std::move(mask); }
    void setMode(Mode mode) { mode_ = mode; }
    void setWeight(float weight) { weight_ = weight; }

    void update(float deltaTime) override;
    void evaluate(const LocalPose& bindPose, LocalPose& outPose) const override;
    float normalizedTime() const override;
    void seekNormalized(float phase) override;

private:
    std::unique_ptr<AnimNode> base_;
    std::unique_ptr<AnimNode> overlay_;
    BoneMask mask_;
    Mode mode_ = Mode::Override;
    float weight_ = 1.0f;

    mutable LocalPose tempPoseOverlay_;
};

} // namespace saida
