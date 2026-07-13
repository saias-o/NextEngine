#pragma once

#include "scene/animation/AnimNode.hpp"
#include <memory>
#include <unordered_map>
#include <string>
#include <vector>
#include "scene/animation/AnimBlackboard.hpp"

namespace saida {

enum class ConditionOp {
    Equals,
    NotEquals,
    Greater,
    Less,
    GreaterEquals,
    LessEquals
};

struct AnimCondition {
    uint32_t paramHash;
    ConditionOp op;
    float value;
    // Les triggers sont remis à zéro quand la transition est prise.
    bool isTrigger = false;

    bool evaluate(const AnimBlackboard* blackboard) const {
        if (!blackboard) return false;
        float paramVal = blackboard->getFloat(paramHash);
        switch(op) {
            case ConditionOp::Equals: return paramVal == value;
            case ConditionOp::NotEquals: return paramVal != value;
            case ConditionOp::Greater: return paramVal > value;
            case ConditionOp::Less: return paramVal < value;
            case ConditionOp::GreaterEquals: return paramVal >= value;
            case ConditionOp::LessEquals: return paramVal <= value;
            default: return false;
        }
    }
};

struct AnimTransition {
    std::string targetState;
    std::vector<AnimCondition> conditions;
    float crossfadeDuration = 0.0f;
    // Phase normalisée minimale de l'état courant avant de pouvoir sortir
    // (< 0 = pas de contrainte). Une transition sans condition mais avec un
    // exit time est le retour automatique d'un one-shot.
    float exitTime = -1.0f;
    // Démarre l'état cible à la phase de l'état source (marche/course).
    bool syncPhase = false;

    bool evaluate(const AnimBlackboard* blackboard) const {
        if (conditions.empty()) return exitTime >= 0.0f;
        for (const auto& cond : conditions) {
            if (!cond.evaluate(blackboard)) return false;
        }
        return true;
    }
};

// An AnimState wraps a specific AnimNode inside a State Machine.
class AnimState {
public:
    AnimState(std::string name, std::unique_ptr<AnimNode> node) 
        : name_(std::move(name)), node_(std::move(node)) {}

    void update(float deltaTime) {
        if (node_) node_->update(deltaTime);
    }

    void evaluate(const LocalPose& bindPose, LocalPose& outPose) const {
        if (node_) {
            node_->evaluate(bindPose, outPose);
        } else {
            // Fallback to bind pose if the node is empty
            outPose.resize(bindPose.localTransforms.size());
            for (size_t i = 0; i < outPose.localTransforms.size(); ++i) {
                outPose.localTransforms[i] = bindPose.localTransforms[i];
            }
        }
    }

    const std::string& name() const { return name_; }
    AnimNode* node() const { return node_.get(); }
    float normalizedTime() const { return node_ ? node_->normalizedTime() : -1.0f; }

    void addTransition(AnimTransition transition) {
        transitions_.push_back(std::move(transition));
    }

    const std::vector<AnimTransition>& transitions() const { return transitions_; }

private:
    std::string name_;
    std::unique_ptr<AnimNode> node_;
    std::vector<AnimTransition> transitions_;
};

// AnimStateMachine manages multiple AnimStates and handles smooth crossfading transitions between them.
class AnimStateMachine : public AnimNode {
public:
    AnimStateMachine() = default;

    void addState(std::unique_ptr<AnimState> state);
    void transitionTo(const std::string& stateName, float crossfadeDuration = 0.0f);

    void setBlackboard(AnimBlackboard* blackboard) { blackboard_ = blackboard; }

    AnimState* currentState() const { return currentState_; }

    void update(float deltaTime) override;
    void evaluate(const LocalPose& bindPose, LocalPose& outPose) const override;

private:
    std::unordered_map<std::string, std::unique_ptr<AnimState>> states_;
    AnimBlackboard* blackboard_ = nullptr;
    
    AnimState* currentState_ = nullptr;
    AnimState* previousState_ = nullptr;
    
    float crossfadeTime_ = 0.0f;
    float crossfadeDuration_ = 0.0f;
    
    mutable LocalPose tempPoseCurrent_;
    mutable LocalPose tempPosePrevious_;
};

} // namespace saida
