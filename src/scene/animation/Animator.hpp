#pragma once

#include "scene/Behaviour.hpp"
#include "scene/animation/Rig.hpp"
#include "scene/animation/Pose.hpp"
#include "scene/animation/AnimStateMachine.hpp"
#include "scene/animation/AnimBlackboard.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace ne {

class AnimationClip;

// The Animator drives an animation graph (FSM / blend tree) for a skinned mesh
// and produces the GlobalPose (skinning matrices) the renderer uploads to the GPU.
//
// Two ways to use it:
//   • Simple, by name — `animator->play("Walk")` / `play("Idle")`. The loader
//     registers the model's clips via addClip(), so a character switches
//     idle/walk/jump from a behaviour with automatic crossfades.
//   • Advanced — provide your own AnimStateMachine via setStateMachine() and drive
//     it with blackboard parameters (setFloat/setBool) + conditions.
class Animator : public Behaviour {
public:
    void onUpdate(float dt) override;

    void setRig(Rig* rig);
    const Rig* rig() const { return rig_; }

    // Bind pose fallback used during evaluation (defaults to identity transforms).
    void setBindPose(LocalPose bindPose) { bindPose_ = std::move(bindPose); }

    // ── Clip library (filled by the loader from the model's animations) ──────
    void addClip(const std::string& name, const AnimationClip* clip) { clips_[name] = clip; }
    const std::unordered_map<std::string, const AnimationClip*>& clips() const { return clips_; }

    // ── Simple playback by name (crossfades from the current clip) ───────────
    void play(const std::string& name, bool loop = true, float crossfade = 0.2f);
    const std::string& currentClip() const { return currentClip_; }

    // ── Advanced graph control ───────────────────────────────────────────────
    void setRootNode(std::unique_ptr<AnimNode> rootNode);
    void setStateMachine(std::unique_ptr<AnimStateMachine> sm);  // wires the blackboard

    AnimBlackboard& blackboard() { return blackboard_; }
    void setFloat(std::string_view p, float v) { blackboard_.setFloat(p, v); }
    void setBool(std::string_view p, bool v) { blackboard_.setBool(p, v); }

    const GlobalPose& globalPose() const { return globalPose_; }
    AnimNode* rootNode() const { return rootNode_.get(); }

    const char* typeName() const override { return "Animator"; }

private:
    Rig* rig_ = nullptr;
    std::unique_ptr<AnimNode> rootNode_;

    // Clip library + the lazily-built FSM that backs play()-by-name.
    std::unordered_map<std::string, const AnimationClip*> clips_;
    AnimStateMachine* playbackFsm_ = nullptr;  // non-owning; == rootNode_ when in use
    std::unordered_set<std::string> playStates_;
    std::string currentClip_;

    AnimBlackboard blackboard_;
    LocalPose bindPose_;
    LocalPose currentLocalPose_;
    GlobalPose globalPose_;
};

} // namespace ne
