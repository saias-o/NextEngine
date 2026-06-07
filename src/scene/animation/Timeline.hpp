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

// FAKE PROPERTY TRACK:
// As per architecture guidelines, this is a placeholder for a future reflection-based property track.
// It will eventually drive arbitrary properties like "Light.intensity" or "Material.roughness".
class TimelinePropertyTrack : public TimelineTrack {
public:
    explicit TimelinePropertyTrack(std::string targetPath) : targetPath_(std::move(targetPath)) {}

    void evaluate(float time) override {
        // TODO(Reflection): Parse targetPath_ (e.g. "Light.intensity"), interpolate keyframes, 
        // and apply the value. Faked for now to keep foundations solid and avoid God Classes.
        (void)time;
    }

    const std::string& targetPath() const { return targetPath_; }

private:
    std::string targetPath_;
};

// A Timeline represents a cinematic sequence or generic multi-track animation.
// It is completely distinct from skeletal AnimationClip, allowing it to sequence anything.
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
