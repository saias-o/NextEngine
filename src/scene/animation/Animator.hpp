#pragma once

#include "scene/Behaviour.hpp"
#include "scene/animation/Rig.hpp"
#include "scene/animation/Pose.hpp"
#include "scene/animation/AnimStateMachine.hpp"
#include <memory>

namespace ne {

// The Animator component manages the playback of an animation graph (e.g. AnimStateMachine)
// and updates the GlobalPose for skinning on the GPU.
class Animator : public Behaviour {
public:
    void onUpdate(float dt) override;

    void setRig(Rig* rig);
    void setRootNode(std::unique_ptr<AnimNode> rootNode);

    // Provide the skeleton's bind pose to use as a fallback during evaluation.
    void setBindPose(LocalPose bindPose) { bindPose_ = std::move(bindPose); }

    const GlobalPose& globalPose() const { return globalPose_; }
    const Rig* rig() const { return rig_; }
    AnimNode* rootNode() const { return rootNode_.get(); }

    const char* typeName() const override { return "Animator"; }

private:
    Rig* rig_ = nullptr;
    std::unique_ptr<AnimNode> rootNode_;

    LocalPose bindPose_;
    LocalPose currentLocalPose_;
    GlobalPose globalPose_;
};

} // namespace ne
