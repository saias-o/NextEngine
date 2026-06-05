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
    float velocityY = 0.0f;
    bool isGrounded = true;
};

} // namespace ne
