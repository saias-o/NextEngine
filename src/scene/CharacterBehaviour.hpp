#pragma once

#include "scene/Behaviour.hpp"

#include <string>

namespace ne {

class Animator;

class CharacterBehaviour : public Behaviour {
public:
    CharacterBehaviour() = default;
    ~CharacterBehaviour() override = default;

    void onReady() override;
    void onUpdate(float dt) override;
    void onDrawInspector() override;

    // Serialize / Deserialize
    const char* typeName() const override { return "Character"; }
    void save(nlohmann::json& j) const override;
    void load(const nlohmann::json& j) override;

    float moveSpeed = 5.0f;
    float jumpForce = 5.0f;
    float gravity = 9.81f;

    // Animation clips played from a child Animator based on movement state. Empty
    // or missing names are simply skipped (animation is optional).
    std::string idleClip = "Idle";
    std::string walkClip = "Walk";
    std::string jumpClip = "Jump";

private:
    void updateAnimation(bool onFloor, bool moving);

    bool warned_ = false;     // warn once if attached to a non-CharacterBody node
    Animator* animator_ = nullptr;  // cached child Animator (found lazily)
};

} // namespace ne
