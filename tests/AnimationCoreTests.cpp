#include "scene/BVHLoader.hpp"
#include "scene/GLTFLoader.hpp"
#include "scene/animation/AnimationClip.hpp"
#include "scene/animation/Animator.hpp"
#include "scene/animation/ClipNode.hpp"
#include "scene/animation/Pose.hpp"
#include "scene/animation/Rig.hpp"

#include <cassert>
#include <cmath>
#include <memory>
#include <string>

namespace {

bool near(float a, float b) {
    return std::abs(a - b) < 1e-5f;
}

saida::Rig makeOutOfOrderRig() {
    saida::Rig rig;

    saida::Transform child;
    child.position = {0.0f, 2.0f, 0.0f};
    rig.addBone("child", 1, glm::mat4(1.0f), child);

    saida::Transform root;
    root.position = {1.0f, 0.0f, 0.0f};
    rig.addBone("root", -1, glm::mat4(1.0f), root);

    std::string error;
    assert(rig.finalize(&error));
    return rig;
}

void testOutOfOrderHierarchy() {
    saida::Rig rig = makeOutOfOrderRig();
    assert(rig.evaluationOrder().size() == 2);
    assert(rig.evaluationOrder()[0] == 1);
    assert(rig.evaluationOrder()[1] == 0);

    saida::LocalPose pose;
    pose.resize(rig.boneCount());
    for (size_t i = 0; i < rig.boneCount(); ++i)
        pose.localTransforms[i] = rig.bones()[i].restLocal;

    saida::GlobalPose global;
    global.computeFrom(pose, rig);

    assert(near(global.globalMatrices[1][3].x, 1.0f));
    assert(near(global.globalMatrices[1][3].y, 0.0f));
    assert(near(global.globalMatrices[0][3].x, 1.0f));
    assert(near(global.globalMatrices[0][3].y, 2.0f));
}

void testAnimatorUsesRigRestPose() {
    saida::Rig rig = makeOutOfOrderRig();
    saida::Animator animator;
    animator.setRig(&rig);
    animator.onUpdate(0.0f);

    const auto& pose = animator.globalPose();
    assert(pose.globalMatrices.size() == 2);
    assert(near(pose.globalMatrices[0][3].x, 1.0f));
    assert(near(pose.globalMatrices[0][3].y, 2.0f));
}

void testInvalidRigCycle() {
    saida::Rig rig;
    rig.addBone("a", 1, glm::mat4(1.0f));
    rig.addBone("b", 0, glm::mat4(1.0f));

    std::string error;
    assert(!rig.finalize(&error));
    assert(!error.empty());
}

void testStepInterpolation() {
    saida::TypedAnimTrack<glm::vec3> track;
    track.target = saida::TrackTarget::Translation;
    track.interpolation = saida::TrackInterpolation::Step;
    track.timestamps = {0.0f, 1.0f};
    track.values = {{0.0f, 0.0f, 0.0f}, {10.0f, 0.0f, 0.0f}};

    saida::Transform sampled;
    track.evaluate(0.75f, sampled);
    assert(near(sampled.position.x, 0.0f));

    track.evaluate(1.0f, sampled);
    assert(near(sampled.position.x, 10.0f));
}

void testLinearInterpolation() {
    saida::TypedAnimTrack<glm::vec3> track;
    track.target = saida::TrackTarget::Translation;
    track.timestamps = {0.0f, 1.0f};
    track.values = {{0.0f, 0.0f, 0.0f}, {10.0f, 0.0f, 0.0f}};

    saida::Transform sampled;
    track.evaluate(0.5f, sampled);
    assert(near(sampled.position.x, 5.0f));
}

// Golden poses from the versioned glTF fixture: a two-joint skin whose single
// animation exercises LINEAR, STEP and CUBICSPLINE channels. The same file must
// produce the same poses on desktop and web (Étape 0 exit criterion).
void testGltfFixturePoses() {
    saida::GltfAnimationData data;
    std::string error;
    const std::string path = std::string(SAIDA_FIXTURE_DIR) + "/animation/two_bone.gltf";
    assert(saida::GLTFLoader::loadAnimationData(path, data, &error));
    assert(data.rigs.size() == 1);
    assert(data.clips.size() == 1);

    saida::Rig& rig = *data.rigs[0];
    assert(rig.boneCount() == 2);
    assert(rig.findBoneIndex("joint_root") == 0);
    assert(rig.findBoneIndex("joint_child") == 1);
    assert(near(rig.bones()[1].restLocal.position.y, 1.0f));

    const saida::AnimationClip* clip = data.clips[0].get();
    assert(clip->name() == "Mix");
    assert(near(clip->duration(), 1.0f));

    saida::ClipNode node(clip, rig);
    saida::LocalPose bind;
    bind.resize(rig.boneCount());
    for (size_t i = 0; i < rig.boneCount(); ++i)
        bind.localTransforms[i] = rig.bones()[i].restLocal;

    saida::LocalPose local;
    saida::GlobalPose global;

    // t = 0.5 — cubic root at the zero-tangent Hermite midpoint (0.5,0,0), linear
    // child at (0,1.5,0), STEP rotation still holding the first key (identity).
    node.setTime(0.5f);
    node.evaluate(bind, local);
    global.computeFrom(local, rig);
    assert(near(global.globalMatrices[0][3].x, 0.5f));
    assert(near(global.globalMatrices[1][3].x, 0.5f));
    assert(near(global.globalMatrices[1][3].y, 1.5f));
    assert(near(global.globalMatrices[1][0].y, 0.0f));  // X axis unrotated

    // t = 1.0 — root (1,0,0), child (1,2,0), STEP rotation now 90° around Z.
    node.setTime(1.0f);
    node.evaluate(bind, local);
    global.computeFrom(local, rig);
    assert(near(global.globalMatrices[0][3].x, 1.0f));
    assert(near(global.globalMatrices[1][3].x, 1.0f));
    assert(near(global.globalMatrices[1][3].y, 2.0f));
    assert(near(global.globalMatrices[1][0].y, 1.0f));  // X axis rotated onto +Y
}

// Golden values from the versioned BVH fixture (Hips 6 channels, Spine 3).
void testBvhFixtureClip() {
    const std::string path = std::string(SAIDA_FIXTURE_DIR) + "/animation/two_joint.bvh";
    std::unique_ptr<saida::AnimationClip> clip = saida::BVHLoader::parse(path);
    assert(clip);
    assert(near(clip->duration(), 1.0f));  // 3 frames @ 0.5s

    assert(clip->getTracks("Hips") != nullptr);
    assert(clip->getTracks("Spine") != nullptr);
    assert(clip->getTracks("Hips")->size() == 2);   // translation + rotation
    assert(clip->getTracks("Spine")->size() == 1);  // rotation only

    // Hips translation is linear between frames: t=0.25 → halfway to (0,1,0).
    saida::Transform hips;
    for (const auto& track : *clip->getTracks("Hips")) track->evaluate(0.25f, hips);
    assert(near(hips.position.y, 0.5f));

    // Frame 1 (t=0.5): Hips rotated 90° around Z.
    saida::Transform hipsF1;
    for (const auto& track : *clip->getTracks("Hips")) track->evaluate(0.5f, hipsF1);
    const glm::vec3 xAxis = hipsF1.rotation * glm::vec3(1, 0, 0);
    assert(near(xAxis.y, 1.0f));

    // Frame 2 (t=1.0): Spine rotated 90° around Z, Hips back to identity.
    saida::Transform spineF2;
    for (const auto& track : *clip->getTracks("Spine")) track->evaluate(1.0f, spineF2);
    const glm::vec3 spineX = spineF2.rotation * glm::vec3(1, 0, 0);
    assert(near(spineX.y, 1.0f));
}

} // namespace

int main() {
    testOutOfOrderHierarchy();
    testAnimatorUsesRigRestPose();
    testInvalidRigCycle();
    testStepInterpolation();
    testLinearInterpolation();
    testGltfFixturePoses();
    testBvhFixtureClip();
    return 0;
}
