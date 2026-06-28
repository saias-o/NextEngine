#include "generated/VehicleBehaviour.hpp"

#include "scene/Node.hpp"
#include "scene/SceneTree.hpp"
#include "scene/HealthBehaviour.hpp"
#include "physics/CharacterBodyNode.hpp"
#include "core/Input.hpp"
#include "core/Log.hpp"

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace ne {

namespace {
constexpr glm::vec3 kWorldUp{0.0f, 1.0f, 0.0f};
constexpr glm::vec3 kForward{0.0f, 0.0f, -1.0f};  // model/engine forward convention

// Move `value` toward `target` by at most `maxDelta` (no overshoot).
float moveToward(float value, float target, float maxDelta) {
    if (std::abs(target - value) <= maxDelta) return target;
    return value + (target > value ? maxDelta : -maxDelta);
}
}  // namespace

float VehicleBehaviour::yawFromRotation() const {
    // forward = R * (0,0,-1) = (-sin yaw, 0, -cos yaw) for a yaw about +Y.
    glm::vec3 f = node()->transform().rotation * kForward;
    return std::atan2(-f.x, -f.z);
}

void VehicleBehaviour::onReady() {
    // Driving reuses the movement actions; bind the boarding key (additive, so it
    // is harmless if the player's CharacterBehaviour already bound the rest).
    Input::bindKey("EnterVehicle", KeyCode::F);
    Input::bindKey("EnterVehicle", KeyCode::E);
}

void VehicleBehaviour::onUpdate(float dt) {
    CharacterBodyNode* body = node() ? node()->asCharacterBody() : nullptr;
    if (!body) {
        if (!warned_) {
            Log::warn("VehicleBehaviour must be on a CharacterBody node — ignored");
            warned_ = true;
        }
        return;
    }

    if (!yawInit_) { yaw_ = yawFromRotation(); yawInit_ = true; }

    const bool occupied = occupant_ != nullptr;

    if (Input::isActionJustPressed("EnterVehicle")) {
        if (!occupied) {
            if (SceneTree* t = tree()) {
                Node* driver = t->firstInGroup(driverGroup);
                // Only an on-foot driver (enabled) standing close enough can board.
                if (driver && driver->enabled()) {
                    glm::vec3 carPos = glm::vec3(node()->worldTransform()[3]);
                    glm::vec3 drvPos = glm::vec3(driver->worldTransform()[3]);
                    if (glm::distance(carPos, drvPos) <= enterRadius)
                        enterVehicle(driver);
                }
            }
        } else {
            exitVehicle();
        }
    }

    const bool driving = occupant_ != nullptr;

    float throttle = driving ? Input::getAxis("MoveBackward", "MoveForward") : 0.0f;  // +fwd
    float steer = driving ? Input::getAxis("MoveLeft", "MoveRight") : 0.0f;           // +right

    float targetSpeed = throttle >= 0.0f ? throttle * maxSpeed : throttle * reverseSpeed;
    bool braking = (targetSpeed == 0.0f) ||
                   (std::abs(targetSpeed) < std::abs(speed_)) ||
                   (targetSpeed * speed_ < 0.0f);
    float rate = braking ? brakeDecel : accel;
    speed_ = moveToward(speed_, targetSpeed, rate * dt);

    // Steering scales with speed (and flips in reverse, like a real car). No turn
    // at a standstill.
    float steerFactor = std::clamp(speed_ / (0.35f * maxSpeed), -1.0f, 1.0f);
    yaw_ -= steer * glm::radians(turnRate) * dt * steerFactor;

    glm::quat rot = glm::angleAxis(yaw_, kWorldUp);
    node()->transform().rotation = rot;
    glm::vec3 forward = rot * kForward;

    glm::vec3 v = body->velocity;
    v.x = forward.x * speed_;
    v.z = forward.z * speed_;
    if (body->isOnFloor()) {
        if (v.y < 0.0f) v.y = 0.0f;
    } else {
        v.y -= gravity * dt;
    }
    body->velocity = v;

    if (std::abs(speed_) >= killSpeed) {
        if (SceneTree* t = tree()) {
            glm::vec3 carPos = glm::vec3(node()->worldTransform()[3]);
            for (Node* npc : t->overlapSphere(carPos, runOverRadius, npcGroup))
                applyDamage(npc, runOverDamage);
        }
    }
}

void VehicleBehaviour::enterVehicle(Node* driver) {
    occupant_ = driver;
    driver->setEnabled(false);              // hide the on-foot body + stop its control
    driver->removeFromGroup(cameraGroup);
    node()->addToGroup(cameraGroup);        // camera now follows the car
    Log::info("Entered vehicle '", node()->name(), "'");
}

void VehicleBehaviour::exitVehicle() {
    Node* driver = occupant_;
    occupant_ = nullptr;
    speed_ = 0.0f;
    if (CharacterBodyNode* body = node()->asCharacterBody()) body->velocity = glm::vec3(0.0f);

    node()->removeFromGroup(cameraGroup);
    if (!driver) return;

    // Pop the driver out beside the car (assumes top-level bodies: local == world).
    glm::vec3 carPos = glm::vec3(node()->worldTransform()[3]);
    glm::vec3 rightDir = node()->transform().rotation * glm::vec3(1.0f, 0.0f, 0.0f);
    driver->transform().position = carPos + rightDir * exitDistance + glm::vec3(0.0f, 0.5f, 0.0f);
    driver->setEnabled(true);
    driver->addToGroup(cameraGroup);        // camera follows the driver again
    Log::info("Left vehicle '", node()->name(), "'");
}

void VehicleBehaviour::onDisable() {
    // If the car node itself is disabled mid-drive, make sure the driver isn't lost.
    if (occupant_) exitVehicle();
}

void VehicleBehaviour::describe(reflect::TypeBuilder<VehicleBehaviour>& t) {
    t.doc("Drivable GTA-style arcade car. Attach to a CharacterBody (group "
          "'vehicle') with a box CollisionShape. Press F/E near it (as the node in "
          "'driverGroup') to board; WASD drives; F/E again to exit.");
    t.property("maxSpeed", &VehicleBehaviour::maxSpeed).range(0.0, 80.0).tooltip("m/s forward top speed");
    t.property("reverseSpeed", &VehicleBehaviour::reverseSpeed).range(0.0, 40.0).tooltip("m/s reverse top speed");
    t.property("accel", &VehicleBehaviour::accel).range(0.0, 100.0).tooltip("m/s^2 acceleration");
    t.property("brakeDecel", &VehicleBehaviour::brakeDecel).range(0.0, 200.0).tooltip("m/s^2 braking");
    t.property("turnRate", &VehicleBehaviour::turnRate).range(0.0, 360.0).tooltip("deg/s steering");
    t.property("gravity", &VehicleBehaviour::gravity).range(0.0, 50.0);
    t.property("enterRadius", &VehicleBehaviour::enterRadius).range(0.0, 15.0).tooltip("boarding distance");
    t.property("exitDistance", &VehicleBehaviour::exitDistance).range(0.0, 10.0);
    t.property("driverGroup", &VehicleBehaviour::driverGroup).tooltip("group of the on-foot character");
    t.property("cameraGroup", &VehicleBehaviour::cameraGroup).tooltip("follow-camera target group");
    t.property("killSpeed", &VehicleBehaviour::killSpeed).range(0.0, 50.0)
        .tooltip("min speed (m/s) to run over NPCs");
    t.property("runOverRadius", &VehicleBehaviour::runOverRadius).range(0.0, 10.0);
    t.property("runOverDamage", &VehicleBehaviour::runOverDamage).range(0.0, 10000.0)
        .tooltip("damage per hit (high = instant kill)");
    t.property("npcGroup", &VehicleBehaviour::npcGroup).tooltip("group of pedestrians (need Health)");
}

} // namespace ne
