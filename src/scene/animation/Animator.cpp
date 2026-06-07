#include "scene/animation/Animator.hpp"
#include "scene/Node.hpp"

namespace ne {

void Animator::setRig(Rig* rig) {
    rig_ = rig;
    if (rig_) {
        // Initialize bind pose with identity transforms if not already set
        if (bindPose_.localTransforms.empty()) {
            bindPose_.localTransforms.resize(rig_->bones().size());
        }
        currentLocalPose_.localTransforms.resize(rig_->bones().size());
        globalPose_.resize(rig_->bones().size());
    }
}

void Animator::setRootNode(std::unique_ptr<AnimNode> rootNode) {
    rootNode_ = std::move(rootNode);
}

void Animator::onUpdate(float dt) {
    if (!rig_) return;

    if (rootNode_) {
        // 1. Advance logic/time
        rootNode_->update(dt);
        
        // 2. Evaluate the pose
        rootNode_->evaluate(bindPose_, currentLocalPose_);
    } else {
        // Fallback to bind pose if no animation graph is present
        currentLocalPose_ = bindPose_;
    }

    // 3. Compute GlobalPose and SkinningMatrices
    // We use identity as the base transform because the renderer applies the entity's model matrix 
    // to the whole mesh *after* skinning is applied in local space.
    globalPose_.computeFrom(currentLocalPose_, *rig_, glm::mat4(1.0f));
}

} // namespace ne
