#pragma once

#include "scene/animation/AnimNode.hpp"
#include "scene/animation/AnimationClip.hpp"
#include "scene/animation/Rig.hpp"
#include "scene/animation/Retarget.hpp"

#include <vector>

namespace saida {

// ClipNode plays back a single immutable AnimationClip. Pass a RetargetMap to play
// a clip whose joint names differ from the rig's (name-based retargeting); null =
// names must match.
class ClipNode : public AnimNode {
public:
    explicit ClipNode(const AnimationClip* clip, const Rig& rig,
                      const RetargetMap* retarget = nullptr);

    void update(float deltaTime) override;
    void evaluate(const LocalPose& bindPose, LocalPose& outPose) const override;

    void setPlaybackSpeed(float speed) { speed_ = speed; }
    void setLooping(bool loop) { looping_ = loop; }
    
    // Forcibly sets the internal playhead time.
    void setTime(float t) { time_ = t; }
    float time() const { return time_; }
    float duration() const { return clip_ ? clip_->duration() : 0.0f; }

private:
    const AnimationClip* clip_ = nullptr;
    float time_ = 0.0f;
    float speed_ = 1.0f;
    bool looping_ = true;

    // Mapping from Rig bone index to list of tracks
    std::vector<const std::vector<std::unique_ptr<AnimTrack>>*> boundTracks_;
};

} // namespace saida
