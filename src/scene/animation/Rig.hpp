#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>
#include <glm/glm.hpp>

namespace ne {

struct Bone {
    std::string name;
    int32_t parentIndex = -1; // -1 if this is a root bone
    glm::mat4 inverseBindMatrix{1.0f}; // Transforms from mesh space to local bone space
};

// Flat skeletal hierarchy. Parents must appear before their children.
class Rig {
public:
    void addBone(const std::string& name, int32_t parentIndex, const glm::mat4& invBind) {
        bones_.push_back({name, parentIndex, invBind});
    }

    const std::vector<Bone>& bones() const { return bones_; }
    size_t boneCount() const { return bones_.size(); }
    
    int32_t findBoneIndex(const std::string& name) const {
        for (size_t i = 0; i < bones_.size(); ++i) {
            if (bones_[i].name == name) {
                return static_cast<int32_t>(i);
            }
        }
        return -1;
    }

private:
    std::vector<Bone> bones_;
};

} // namespace ne
