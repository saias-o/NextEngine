#include "scene/CharacterBehaviour.hpp"
#include "scene/Node.hpp"
#include "physics/CharacterBodyNode.hpp"
#include "scene/animation/Animator.hpp"
#include "core/Input.hpp"
#include "core/Log.hpp"

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <glm/glm.hpp>

#include <cstring>

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

    bool moving = glm::length(glm::vec2(v.x, v.z)) > 0.1f;
    updateAnimation(body->isOnFloor(), moving);
}

void CharacterBehaviour::updateAnimation(bool onFloor, bool moving) {
    if (!animator_) animator_ = node()->findBehaviourInChildren<Animator>();
    if (!animator_) return;  // no skinned character → nothing to drive

    const std::string& want = !onFloor ? jumpClip : (moving ? walkClip : idleClip);
    if (!want.empty() && animator_->clips().count(want))
        animator_->play(want);  // play() no-ops if it's already the current clip
}

void CharacterBehaviour::onDrawInspector() {
    ImGui::DragFloat("Move Speed", &moveSpeed, 0.1f, 0.1f, 50.0f);
    ImGui::DragFloat("Jump Force", &jumpForce, 0.1f, 0.1f, 50.0f);
    ImGui::DragFloat("Gravity", &gravity, 0.1f, 0.1f, 50.0f);
    if (CharacterBodyNode* body = node() ? node()->asCharacterBody() : nullptr)
        ImGui::TextDisabled("On floor: %s", body->isOnFloor() ? "yes" : "no");
    else
        ImGui::TextDisabled("(attach to a CharacterBody node)");

    ImGui::SeparatorText("Animation clips (child Animator)");
    auto clipField = [](const char* label, std::string& s) {
        char buf[64];
        std::strncpy(buf, s.c_str(), sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        if (ImGui::InputText(label, buf, sizeof(buf))) s = buf;
    };
    clipField("Idle", idleClip);
    clipField("Walk", walkClip);
    clipField("Jump", jumpClip);
}

void CharacterBehaviour::save(nlohmann::json& j) const {
    j["moveSpeed"] = moveSpeed;
    j["jumpForce"] = jumpForce;
    j["gravity"] = gravity;
    j["idleClip"] = idleClip;
    j["walkClip"] = walkClip;
    j["jumpClip"] = jumpClip;
}

void CharacterBehaviour::load(const nlohmann::json& j) {
    if (j.contains("moveSpeed")) moveSpeed = j["moveSpeed"].get<float>();
    if (j.contains("jumpForce")) jumpForce = j["jumpForce"].get<float>();
    if (j.contains("gravity")) gravity = j["gravity"].get<float>();
    if (j.contains("idleClip")) idleClip = j["idleClip"].get<std::string>();
    if (j.contains("walkClip")) walkClip = j["walkClip"].get<std::string>();
    if (j.contains("jumpClip")) jumpClip = j["jumpClip"].get<std::string>();
}

} // namespace ne
