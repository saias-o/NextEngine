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

void ClipNode::setRange(float start, float end) {
    rangeStart_ = start;
    rangeEnd_ = end;
}

float ClipNode::rangeEnd() const {
    return rangeEnd_ >= 0.0f ? rangeEnd_ : duration();
}

void ClipNode::update(float deltaTime) {
    if (!clip_) return;

    // Advance time
    time_ += deltaTime * speed_;

    // Loop or clamp inside the playable window ([0, duration] or the view range).
    const float start = rangeStart_;
    const float end = rangeEnd();
    const float span = end - start;
    if (span > 0.0f) {
        if (looping_) {
            time_ = start + std::fmod(time_ - start, span);
            if (time_ < start) {
                time_ += span;
            }
        } else {
            if (time_ > end) time_ = end;
            if (time_ < start) time_ = start;
        }
    } else {
        time_ = start;
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
