#pragma once

#include <vector>
#include <string>
#include <memory>

namespace ne {

// Base class for a generic track in a timeline.
class TimelineTrack {
public:
    virtual ~TimelineTrack() = default;
    
    // Evaluate the track at a specific time.
    virtual void evaluate(float time) = 0;
};

// Placeholder for future reflection-driven property tracks.
class TimelinePropertyTrack : public TimelineTrack {
public:
    explicit TimelinePropertyTrack(std::string targetPath) : targetPath_(std::move(targetPath)) {}

    void evaluate(float time) override {
        // TODO(Reflection): parse targetPath_, interpolate keyframes, and apply the value.
        (void)time;
    }

    const std::string& targetPath() const { return targetPath_; }

private:
    std::string targetPath_;
};

// Cinematic/generic multi-track animation, separate from skeletal AnimationClip.
class Timeline {
public:
    void addTrack(std::unique_ptr<TimelineTrack> track) {
        tracks_.push_back(std::move(track));
    }

    void evaluate(float time) {
        for (auto& track : tracks_) {
            track->evaluate(time);
        }
    }

    float duration() const { return duration_; }
    void setDuration(float d) { duration_ = d; }

private:
    std::vector<std::unique_ptr<TimelineTrack>> tracks_;
    float duration_ = 0.0f;
};

} // namespace ne
