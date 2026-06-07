#pragma once

#include <vector>
#include <string>
#include <memory>
#include <unordered_map>
#include <cstdint>
#include <cstddef>
#include "scene/Node.hpp" // For Transform
#include "scene/animation/Pose.hpp"

namespace ne {

// Targets for skeletal animation tracks
enum class TrackTarget {
    Translation,
    Rotation,
    Scale
};

// Base interface for a bone animation track
class AnimTrack {
public:
    virtual ~AnimTrack() = default;
    
    // Evaluates the track at a given time and modifies the relevant part of outTransform.
    // NOTE: This assumes outTransform is already initialized to a sensible default (e.g. Bind Pose).
    virtual void evaluate(float time, Transform& outTransform) const = 0;
};

// Templated track to handle vec3 (Translation/Scale) or quat (Rotation)
template <typename T>
class TypedAnimTrack : public AnimTrack {
public:
    TrackTarget target;
    std::vector<float> timestamps;
    std::vector<T> values;

    void evaluate(float time, Transform& outTransform) const override;
};

// AnimationClip represents an immutable, read-only buffer of animation data.
// It is completely decoupled from any specific entity, allowing it to be shared (e.g. Mixamo run.glb).
class AnimationClip {
public:
    AnimationClip(std::string name, float duration) 
        : name_(std::move(name)), duration_(duration) {}

    // Adds a track for a given bone name.
    // In a retargeted workflow, this name maps to a standard Rig bone name.
    void addTrack(const std::string& boneName, std::unique_ptr<AnimTrack> track);

    float duration() const { return duration_; }
    const std::string& name() const { return name_; }

    const std::vector<std::unique_ptr<AnimTrack>>* getTracks(const std::string& boneName) const;

private:
    std::string name_;
    float duration_ = 0.0f;
    // Storage for bone tracks by name.
    std::unordered_map<std::string, std::vector<std::unique_ptr<AnimTrack>>> boneTracks_;
};

} // namespace ne
