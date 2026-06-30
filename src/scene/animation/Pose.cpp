#include "scene/animation/Pose.hpp"

namespace saida {

void GlobalPose::computeFrom(const LocalPose& localPose, const Rig& rig, const glm::mat4& baseTransform) {
    size_t boneCount = rig.bones().size();
    resize(boneCount);

    for (size_t i = 0; i < boneCount; ++i) {
        const auto& bone = rig.bones()[i];
        
        glm::mat4 localMat = (i < localPose.localTransforms.size()) 
                           ? localPose.localTransforms[i].matrix() 
                           : glm::mat4(1.0f);

        if (bone.parentIndex >= 0 && static_cast<size_t>(bone.parentIndex) < i) {
            globalMatrices[i] = globalMatrices[bone.parentIndex] * localMat;
        } else {
            globalMatrices[i] = baseTransform * localMat;
        }

        skinningMatrices[i] = globalMatrices[i] * bone.inverseBindMatrix;
    }
}

} // namespace saida
