#include "scene/animation/ClipNode.hpp"
#include <cmath>

namespace ne {

ClipNode::ClipNode(const AnimationClip* clip, const Rig& rig) : clip_(clip) {
    if (!clip_) return;
    
    // Bind tracks to rig bones (O(1) mapping setup)
    boundTracks_.resize(rig.boneCount(), nullptr);
    for (size_t i = 0; i < rig.boneCount(); ++i) {
        boundTracks_[i] = clip_->getTracks(rig.bones()[i].name);
    }
}

void ClipNode::update(float deltaTime) {
    if (!clip_) return;

    // Advance time
    time_ += deltaTime * speed_;

    // Handle looping
    float duration = clip_->duration();
    if (duration > 0.0f) {
        if (looping_) {
            time_ = std::fmod(time_, duration);
            if (time_ < 0.0f) {
                time_ += duration;
            }
        } else {
            if (time_ > duration) time_ = duration;
            if (time_ < 0.0f) time_ = 0.0f;
        }
    } else {
        time_ = 0.0f;
    }
}

void ClipNode::evaluate(const LocalPose& bindPose, LocalPose& outPose) const {
    outPose.resize(bindPose.localTransforms.size());

    for (size_t i = 0; i < outPose.localTransforms.size(); ++i) {
        outPose.localTransforms[i] = bindPose.localTransforms[i];

        if (i < boundTracks_.size() && boundTracks_[i] != nullptr) {
            for (const auto& track : *boundTracks_[i]) {
                track->evaluate(time_, outPose.localTransforms[i]);
            }
        }
    }
}

} // namespace ne
