#include "generated/NpcWanderBehaviour.hpp"

#include "scene/Node.hpp"
#include "physics/CharacterBodyNode.hpp"
#include "core/Log.hpp"

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/constants.hpp>

#include <cmath>
#include <cstdint>

namespace ne {

namespace {
constexpr glm::vec3 kWorldUp{0.0f, 1.0f, 0.0f};
constexpr glm::vec3 kForward{0.0f, 0.0f, -1.0f};

// Shortest signed angular difference a-b, wrapped to [-pi, pi].
float angleDelta(float a, float b) {
    float d = std::fmod(a - b + glm::pi<float>(), glm::two_pi<float>());
    if (d < 0.0f) d += glm::two_pi<float>();
    return d - glm::pi<float>();
}
}  // namespace

void NpcWanderBehaviour::onReady() {
    // Seed per-instance so a crowd doesn't move in lockstep.
    rng_.seed(static_cast<uint32_t>(reinterpret_cast<std::uintptr_t>(this)));
}

void NpcWanderBehaviour::pickNewHeading() {
    std::uniform_real_distribution<float> angle(0.0f, glm::two_pi<float>());
    std::uniform_real_distribution<float> jitter(0.5f, 1.5f);
    targetYaw_ = angle(rng_);
    timer_ = changeInterval * jitter(rng_);

    // If we've drifted past the leash, aim roughly back toward home.
    if (leashRadius > 0.0f) {
        glm::vec3 pos = glm::vec3(node()->worldTransform()[3]);
        glm::vec3 toHome = home_ - pos;
        toHome.y = 0.0f;
        if (glm::dot(toHome, toHome) > leashRadius * leashRadius) {
            // yaw such that forward = (-sin yaw, -cos yaw) points toward home.
            glm::vec3 dir = glm::normalize(toHome);
            targetYaw_ = std::atan2(-dir.x, -dir.z);
        }
    }
}

void NpcWanderBehaviour::onUpdate(float dt) {
    CharacterBodyNode* body = node() ? node()->asCharacterBody() : nullptr;
    if (!body) {
        if (!warned_) {
            Log::warn("NpcWanderBehaviour must be on a CharacterBody node — ignored");
            warned_ = true;
        }
        return;
    }

    if (!init_) {
        home_ = glm::vec3(node()->worldTransform()[3]);
        glm::vec3 f = node()->transform().rotation * kForward;
        yaw_ = targetYaw_ = std::atan2(-f.x, -f.z);
        init_ = true;
        pickNewHeading();
    }

    timer_ -= dt;
    if (timer_ <= 0.0f) pickNewHeading();

    // Smoothly turn toward the target heading.
    float a = 1.0f - std::exp(-turnSpeed * dt);
    yaw_ += angleDelta(targetYaw_, yaw_) * a;

    glm::quat rot = glm::angleAxis(yaw_, kWorldUp);
    node()->transform().rotation = rot;
    glm::vec3 forward = rot * kForward;

    glm::vec3 v = body->velocity;
    v.x = forward.x * speed;
    v.z = forward.z * speed;
    if (body->isOnFloor()) {
        if (v.y < 0.0f) v.y = 0.0f;
    } else {
        v.y -= gravity * dt;
    }
    body->velocity = v;
}

void NpcWanderBehaviour::describe(reflect::TypeBuilder<NpcWanderBehaviour>& t) {
    t.doc("Simple wandering pedestrian: picks a random heading periodically and "
          "strolls there. Attach to a CharacterBody (group 'npc').");
    t.property("speed", &NpcWanderBehaviour::speed).range(0.0, 10.0).tooltip("m/s walking speed");
    t.property("changeInterval", &NpcWanderBehaviour::changeInterval).range(0.2, 20.0)
        .tooltip("seconds between heading changes");
    t.property("turnSpeed", &NpcWanderBehaviour::turnSpeed).range(0.0, 30.0);
    t.property("leashRadius", &NpcWanderBehaviour::leashRadius).range(0.0, 100.0)
        .tooltip("stay within this of spawn (0 = roam freely)");
    t.property("gravity", &NpcWanderBehaviour::gravity).range(0.0, 50.0);
}

} // namespace ne
