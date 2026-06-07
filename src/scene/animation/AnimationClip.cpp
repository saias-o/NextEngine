#include "scene/animation/AnimationClip.hpp"
#include <algorithm>
#include <iterator>
#include <glm/gtc/quaternion.hpp> // Required for slerp

namespace ne {

template <typename T>
static T interpolate(const T& a, const T& b, float t);

template <>
glm::vec3 interpolate<glm::vec3>(const glm::vec3& a, const glm::vec3& b, float t) {
    return glm::mix(a, b, t);
}

template <>
glm::quat interpolate<glm::quat>(const glm::quat& a, const glm::quat& b, float t) {
    return glm::slerp(a, b, t);
}

template <typename T>
void TypedAnimTrack<T>::evaluate(float time, Transform& outTransform) const {
    if (timestamps.empty()) return;

    if (time <= timestamps.front()) {
        if constexpr (std::is_same_v<T, glm::vec3>) {
            if (target == TrackTarget::Translation) outTransform.position = values.front();
            else if (target == TrackTarget::Scale) outTransform.scale = values.front();
        } else if constexpr (std::is_same_v<T, glm::quat>) {
            if (target == TrackTarget::Rotation) outTransform.rotation = values.front();
        }
        return;
    }
    
    if (time >= timestamps.back()) {
        if constexpr (std::is_same_v<T, glm::vec3>) {
            if (target == TrackTarget::Translation) outTransform.position = values.back();
            else if (target == TrackTarget::Scale) outTransform.scale = values.back();
        } else if constexpr (std::is_same_v<T, glm::quat>) {
            if (target == TrackTarget::Rotation) outTransform.rotation = values.back();
        }
        return;
    }

    auto it = std::upper_bound(timestamps.begin(), timestamps.end(), time);
    size_t nextIdx = std::distance(timestamps.begin(), it);
    size_t prevIdx = nextIdx - 1;

    float t0 = timestamps[prevIdx];
    float t1 = timestamps[nextIdx];
    float factor = (t1 > t0) ? (time - t0) / (t1 - t0) : 0.0f;

    T val = interpolate(values[prevIdx], values[nextIdx], factor);

    if constexpr (std::is_same_v<T, glm::vec3>) {
        if (target == TrackTarget::Translation) outTransform.position = val;
        else if (target == TrackTarget::Scale) outTransform.scale = val;
    } else if constexpr (std::is_same_v<T, glm::quat>) {
        if (target == TrackTarget::Rotation) outTransform.rotation = val;
    }
}

template class TypedAnimTrack<glm::vec3>;
template class TypedAnimTrack<glm::quat>;

void AnimationClip::addTrack(const std::string& boneName, std::unique_ptr<AnimTrack> track) {
    boneTracks_[boneName].push_back(std::move(track));
}

const std::vector<std::unique_ptr<AnimTrack>>* AnimationClip::getTracks(const std::string& boneName) const {
    auto it = boneTracks_.find(boneName);
    if (it != boneTracks_.end()) {
        return &it->second;
    }
    return nullptr;
}

} // namespace ne
