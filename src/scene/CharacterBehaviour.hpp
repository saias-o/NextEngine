#pragma once

#include "scene/Behaviour.hpp"

namespace ne {

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

private:
    bool warned_ = false;  // warn once if attached to a non-CharacterBody node
};

} // namespace ne
