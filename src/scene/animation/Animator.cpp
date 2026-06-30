#include "scene/animation/Animator.hpp"
#include "scene/animation/ClipNode.hpp"
#include "scene/animation/AnimationClip.hpp"
#include "scene/Node.hpp"
#include "core/Log.hpp"
#include "core/Profiler.hpp"

namespace saida {

void Animator::setRig(Rig* rig) {
    rig_ = rig;
    if (rig_) {
        // Initialize bind pose with identity transforms if not already set.
        if (bindPose_.localTransforms.empty())
            bindPose_.localTransforms.resize(rig_->bones().size());
        currentLocalPose_.localTransforms.resize(rig_->bones().size());
        globalPose_.resize(rig_->bones().size());
    }
}

void Animator::setRootNode(std::unique_ptr<AnimNode> rootNode) {
    rootNode_ = std::move(rootNode);
    playbackFsm_ = nullptr;  // a custom root replaces the by-name playback FSM
    playStates_.clear();
    currentClip_.clear();
}

void Animator::setStateMachine(std::unique_ptr<AnimStateMachine> sm) {
    if (sm) sm->setBlackboard(&blackboard_);
    rootNode_ = std::move(sm);
    playbackFsm_ = nullptr;
    playStates_.clear();
    currentClip_.clear();
}

void Animator::play(const std::string& name, bool loop, float crossfade) {
    if (name == currentClip_) return;
    auto it = clips_.find(name);
    if (it == clips_.end() || !it->second) {
        Log::warn("Animator::play: unknown clip '", name, "'");
        return;
    }
    if (!rig_) return;

    // Back play()-by-name with an internal state machine so we reuse its crossfade.
    if (!playbackFsm_) {
        auto sm = std::make_unique<AnimStateMachine>();
        sm->setBlackboard(&blackboard_);
        playbackFsm_ = sm.get();
        rootNode_ = std::move(sm);
        playStates_.clear();
    }

    if (playStates_.insert(name).second) {
        auto clip = std::make_unique<ClipNode>(it->second, *rig_,
                                               retarget_.empty() ? nullptr : &retarget_);
        clip->setLooping(loop);
        playbackFsm_->addState(std::make_unique<AnimState>(name, std::move(clip)));
    }
    playbackFsm_->transitionTo(name, crossfade);
    currentClip_ = name;
}

void Animator::onUpdate(float dt) {
    SAIDA_PROFILE_SCOPE("Animation/AnimatorUpdate");
    SAIDA_PROFILE_COUNTER_ADD("Animation/Animators", 1);
    if (!rig_) return;

    if (rootNode_) {
        rootNode_->update(dt);
        rootNode_->evaluate(bindPose_, currentLocalPose_);
    } else {
        currentLocalPose_ = bindPose_;  // no graph → rest pose
    }

    // GlobalPose in object space (identity base): the renderer applies the entity
    // model matrix to the whole mesh after skinning.
    globalPose_.computeFrom(currentLocalPose_, *rig_, glm::mat4(1.0f));
}

} // namespace saida
