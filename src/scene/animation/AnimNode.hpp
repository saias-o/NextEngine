#pragma once

#include "scene/animation/Pose.hpp"

namespace saida {

// AnimNode is the base class for any node in the animation graph (FSM, Blend Tree, etc.).
// It follows a Data-Oriented/functional approach where evaluation outputs a LocalPose.
class AnimNode {
public:
    virtual ~AnimNode() = default;

    // Advances the internal playhead/timers.
    virtual void update(float deltaTime) = 0;

    // Evaluates the node without advancing time. Updates outPose.
    // bindPose: The fallback/rest pose of the rig.
    virtual void evaluate(const LocalPose& bindPose, LocalPose& outPose) const = 0;

    // Phase de lecture normalisée [0,1] pour l'exit time et la synchronisation
    // des transitions ; -1 quand la notion n'a pas de sens pour ce nœud.
    virtual float normalizedTime() const { return -1.0f; }
    virtual void seekNormalized(float phase) { (void)phase; }
};

} // namespace saida
