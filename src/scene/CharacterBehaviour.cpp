#include "scene/CharacterBehaviour.hpp"
#include "scene/Node.hpp"
#include "core/Input.hpp"

#include <imgui.h>
#include <nlohmann/json.hpp>
#include <iostream>

namespace ne {

void CharacterBehaviour::onReady() {
    std::cout << "[Character] onReady called! Binding keys..." << std::endl;
    // Note: GLFW KeyCodes are based on physical QWERTY positions.
    // W, A, S, D on QWERTY match the physical positions of Z, Q, S, D on AZERTY.
    Input::bindKey("MoveForward", KeyCode::W);
    Input::bindKey("MoveLeft", KeyCode::A);
    Input::bindKey("MoveBackward", KeyCode::S);
    Input::bindKey("MoveRight", KeyCode::D);
    Input::bindKey("Jump", KeyCode::Space);
}

void CharacterBehaviour::onUpdate(float dt) {
    if (!node()) return;

    // Read input vector
    glm::vec2 move = Input::getVector("MoveLeft", "MoveRight", "MoveBackward", "MoveForward");
    
    if (glm::length(move) > 0.0f) {
        std::cout << "[Character] move: " << move.x << ", " << move.y << std::endl;
    }
    
    // Apply movement
    glm::vec3 direction = glm::vec3(move.x, 0.0f, move.y);
    if (glm::length(direction) > 0.01f) {
        direction = glm::normalize(direction);
    }
    
    node()->transform().position += direction * moveSpeed * dt;

    // Fake physics (Jump)
    if (isGrounded && Input::isActionJustPressed("Jump")) {
        std::cout << "[Character] Jumped!" << std::endl;
        velocityY = jumpForce;
        isGrounded = false;
    }

    if (!isGrounded) {
        velocityY -= gravity * dt;
        node()->transform().position.y += velocityY * dt;

        // Ground collision fake
        if (node()->transform().position.y <= 0.0f) {
            node()->transform().position.y = 0.0f;
            velocityY = 0.0f;
            isGrounded = true;
        }
    }
}

void CharacterBehaviour::onDrawInspector() {
    ImGui::DragFloat("Move Speed", &moveSpeed, 0.1f, 0.1f, 50.0f);
    ImGui::DragFloat("Jump Force", &jumpForce, 0.1f, 0.1f, 50.0f);
    ImGui::DragFloat("Gravity", &gravity, 0.1f, 0.1f, 50.0f);
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
