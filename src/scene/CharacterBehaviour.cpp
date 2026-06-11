#include "scene/CharacterBehaviour.hpp"
#include "scene/Node.hpp"
#include "physics/CharacterBodyNode.hpp"
#include "core/Input.hpp"
#include "core/Log.hpp"

#include <imgui.h>
#include <nlohmann/json.hpp>

namespace ne {

void CharacterBehaviour::onReady() {
    // Default WASD/ZQSD bindings (GLFW key codes are physical QWERTY positions).
    Input::bindKey("MoveForward", KeyCode::W);
    Input::bindKey("MoveLeft", KeyCode::A);
    Input::bindKey("MoveBackward", KeyCode::S);
    Input::bindKey("MoveRight", KeyCode::D);
    Input::bindKey("Jump", KeyCode::Space);
}

void CharacterBehaviour::onUpdate(float dt) {
    CharacterBodyNode* body = node() ? node()->asCharacterBody() : nullptr;
    if (!body) {
        if (!warned_) {
            Log::warn("CharacterBehaviour must be on a CharacterBody node — ignored");
            warned_ = true;
        }
        return;
    }

    // Logic only: read input → write velocity. The engine performs the slide in
    // the physics step (no moveAndSlide to call or forget).
    glm::vec2 in = Input::getVector("MoveLeft", "MoveRight", "MoveBackward", "MoveForward");
    glm::vec3 v = body->velocity;
    v.x = in.x * moveSpeed;
    v.z = -in.y * moveSpeed;  // forward = -Z

    if (body->isOnFloor()) {
        v.y = 0.0f;
        if (Input::isActionJustPressed("Jump")) v.y = jumpForce;
    } else {
        v.y -= gravity * dt;  // gravity while airborne
    }

    body->velocity = v;
}

void CharacterBehaviour::onDrawInspector() {
    ImGui::DragFloat("Move Speed", &moveSpeed, 0.1f, 0.1f, 50.0f);
    ImGui::DragFloat("Jump Force", &jumpForce, 0.1f, 0.1f, 50.0f);
    ImGui::DragFloat("Gravity", &gravity, 0.1f, 0.1f, 50.0f);
    if (CharacterBodyNode* body = node() ? node()->asCharacterBody() : nullptr)
        ImGui::TextDisabled("On floor: %s", body->isOnFloor() ? "yes" : "no");
    else
        ImGui::TextDisabled("(attach to a CharacterBody node)");
}

void CharacterBehaviour::save(nlohmann::json& j) const {
    j["moveSpeed"] = moveSpeed;
    j["jumpForce"] = jumpForce;
    j["gravity"] = gravity;
}

void CharacterBehaviour::load(const nlohmann::json& j) {
    if (j.contains("moveSpeed")) moveSpeed = j["moveSpeed"].get<float>();
    if (j.contains("jumpForce")) jumpForce = j["jumpForce"].get<float>();
    if (j.contains("gravity")) gravity = j["gravity"].get<float>();
}

} // namespace ne
