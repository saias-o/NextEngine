#include "scene/animation/Animator.hpp"
#include "scene/animation/AnimGraphAsset.hpp"
#include "scene/animation/AnimationClip.hpp"
#include "scene/animation/ClipNode.hpp"
#include "scene/animation/ClipView.hpp"
#include "scene/Node.hpp"
#include "core/Log.hpp"
#include "core/Profiler.hpp"

namespace saida {

namespace {

// Nom de clip d'une clé de sous-asset : "modèle.glb#Run" → "Run".
std::string clipNameFromKey(const std::string& key) {
    const size_t hash = key.rfind('#');
    return hash == std::string::npos ? key : key.substr(hash + 1);
}

} // namespace

void Animator::setRig(Rig* rig) {
    rig_ = rig;
    if (!rig_) {
        bindPose_.localTransforms.clear();
        currentLocalPose_.localTransforms.clear();
        globalPose_.globalMatrices.clear();
        globalPose_.skinningMatrices.clear();
        return;
    }

    std::string error;
    if (!rig_->finalized() && !rig_->finalize(&error)) {
        Log::error("Animator: invalid rig: ", error);
        rig_ = nullptr;
        return;
    }

    const size_t boneCount = rig_->boneCount();
    bindPose_.resize(boneCount);
    for (size_t i = 0; i < boneCount; ++i)
        bindPose_.localTransforms[i] = rig_->bones()[i].restLocal;
    currentLocalPose_.resize(boneCount);
    globalPose_.resize(boneCount);
}

void Animator::setRootNode(std::unique_ptr<AnimNode> rootNode) {
    rootNode_ = std::move(rootNode);
    playbackFsm_ = nullptr;  // a custom root replaces the by-name playback FSM
    playStates_.clear();
    currentClip_.clear();
}

void Animator::setStateMachine(std::unique_ptr<AnimStateMachine> sm) {
    if (sm) sm->setBlackboard(&blackboard_);
    rootNode_ = std::move(sm);
    playbackFsm_ = nullptr;
    playStates_.clear();
    currentClip_.clear();
}

void Animator::play(const std::string& name, bool loop, float crossfade) {
    if (name == currentClip_) return;
    auto it = clips_.find(name);
    if (it == clips_.end() || !it->second) {
        Log::warn("Animator::play: unknown clip '", name, "'");
        return;
    }
    if (!rig_) return;

    // Back play()-by-name with an internal state machine so we reuse its crossfade.
    if (!playbackFsm_) {
        auto sm = std::make_unique<AnimStateMachine>();
        sm->setBlackboard(&blackboard_);
        playbackFsm_ = sm.get();
        rootNode_ = std::move(sm);
        playStates_.clear();
    }

    if (playStates_.insert(name).second) {
        auto clip = std::make_unique<ClipNode>(it->second, *rig_,
                                               retarget_.empty() ? nullptr : &retarget_);
        clip->setLooping(loop);
        playbackFsm_->addState(std::make_unique<AnimState>(name, std::move(clip)));
    }
    playbackFsm_->transitionTo(name, crossfade);
    currentClip_ = name;
}

void Animator::playView(const ClipView& view, float crossfade) {
    if (!rig_) return;
    if (view.name.empty()) {
        Log::warn("Animator::playView: view has no name");
        return;
    }
    if (view.name == currentClip_) return;

    const std::string clipName = clipNameFromKey(view.source);
    auto it = clips_.find(clipName);
    if (it == clips_.end() || !it->second) {
        Log::warn("Animator::playView: unknown source clip '", clipName, "' for view '",
                  view.name, "'");
        return;
    }

    // Même FSM interne que play() : les vues et les clips directs partagent
    // l'espace de noms des états.
    if (!playbackFsm_) {
        auto sm = std::make_unique<AnimStateMachine>();
        sm->setBlackboard(&blackboard_);
        playbackFsm_ = sm.get();
        rootNode_ = std::move(sm);
        playStates_.clear();
    }

    if (playStates_.insert(view.name).second) {
        playbackFsm_->addState(
            std::make_unique<AnimState>(view.name, view.instantiate(*it->second, *rig_)));
    }
    playbackFsm_->transitionTo(view.name, crossfade);
    currentClip_ = view.name;
}

bool Animator::setGraph(const AnimGraphAsset& graph,
                        std::vector<AssetDiagnostic>* diagnostics) {
    if (!rig_) {
        if (diagnostics)
            diagnostics->push_back({"animgraph.build.no_rig",
                                    AssetDiagnostic::Severity::Error, "",
                                    "animator has no rig"});
        return false;
    }

    auto sm = graph.build(
        [this](const std::string& key) -> const AnimationClip* {
            auto it = clips_.find(clipNameFromKey(key));
            return it != clips_.end() ? it->second : nullptr;
        },
        *rig_, diagnostics);
    if (!sm) return false;

    for (const auto& p : graph.parameters) blackboard_.setFloat(p.name, p.defaultValue);
    setStateMachine(std::move(sm));
    return true;
}

ClipNode* Animator::activeClipNode() const {
    if (auto* clip = dynamic_cast<ClipNode*>(rootNode_.get())) return clip;
    if (playbackFsm_ && playbackFsm_->currentState())
        return dynamic_cast<ClipNode*>(playbackFsm_->currentState()->node());
    return nullptr;
}

void Animator::onUpdate(float dt) {
    SAIDA_PROFILE_SCOPE("Animation/AnimatorUpdate");
    SAIDA_PROFILE_COUNTER_ADD("Animation/Animators", 1);
    if (!rig_) return;

    if (rootNode_) {
        rootNode_->update(dt);
        rootNode_->evaluate(bindPose_, currentLocalPose_);
    } else {
        currentLocalPose_ = bindPose_;  // no graph → rest pose
    }

    // GlobalPose in object space (identity base): the renderer applies the entity
    // model matrix to the whole mesh after skinning.
    globalPose_.computeFrom(currentLocalPose_, *rig_, glm::mat4(1.0f));
}

} // namespace saida
