#pragma once

#include "scene/Behaviour.hpp"
#include "core/Reflection.hpp"

#include <glm/glm.hpp>

#include <random>

namespace ne {

// A dead-simple wandering pedestrian. Attach to a CharacterBody node (a capsule).
// Every `changeInterval` seconds it picks a new random heading and strolls toward
// it at `speed`, turning smoothly and facing where it walks. The CharacterBody
// handles gravity and sliding against the world, so NPCs stay on the ground and
// bump off buildings without any pathfinding.
//
// If `leashRadius` > 0 the NPC is gently steered back when it wanders too far from
// its spawn point, so a crowd stays roughly where it was placed.
class NpcWanderBehaviour : public Behaviour {
public:
    void onReady() override;
    void onUpdate(float dt) override;

    NE_REFLECT_BEHAVIOUR(NpcWanderBehaviour, "NpcWander")

    float speed = 1.6f;          // m/s walking speed
    float changeInterval = 3.0f; // seconds between heading changes
    float turnSpeed = 6.0f;      // exponential turn rate toward the new heading
    float leashRadius = 12.0f;   // stay within this of spawn (0 = roam freely)
    float gravity = 22.0f;       // m/s^2 downward pull when airborne

private:
    void pickNewHeading();

    glm::vec3 home_{0.0f};
    float yaw_ = 0.0f;        // current heading, radians
    float targetYaw_ = 0.0f; // heading being turned toward
    float timer_ = 0.0f;     // countdown to the next heading change
    bool init_ = false;
    bool warned_ = false;
    std::mt19937 rng_;
};

} // namespace ne
