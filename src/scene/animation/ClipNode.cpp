#include "scene/animation/ClipNode.hpp"
#include <cmath>

namespace saida {

ClipNode::ClipNode(const AnimationClip* clip, const Rig& rig, const RetargetMap* retarget)
    : clip_(clip) {
    if (!clip_) return;

    // Bind tracks to rig bones (O(1) mapping setup). With a retarget map, the rig
    // bone name is translated to the clip's track name first.
    boundTracks_.resize(rig.boneCount(), nullptr);
    for (size_t i = 0; i < rig.boneCount(); ++i) {
        const std::string& rigName = rig.bones()[i].name;
        const std::string& clipName = retarget ? retarget->resolve(rigName) : rigName;
        boundTracks_[i] = clip_->getTracks(clipName);
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

} // namespace saida
